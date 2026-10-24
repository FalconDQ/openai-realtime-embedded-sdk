#ifndef LINUX_BUILD
#include <driver/i2s.h>
#include <esp_heap_caps.h>
#include <freertos/FreeRTOS.h>
#include <freertos/idf_additions.h>
#include <opus.h>
#endif

#include <esp_event.h>
#include <esp_log.h>
#include <string.h>
#include <stdlib.h>

#include "main.h"
#include "oai_peer_stats.h"

#define TICK_INTERVAL 1
#define GREETING                                                    \
  "{\"type\": \"response.create\", \"response\": {\"output_modalities\": " \
  "[\"audio\"], \"instructions\": \"Say 'How can I help?.'\"}}"

PeerConnection *peer_connection = NULL;

extern "C" const char *oai_peer_stage(void);
extern "C" uint32_t oai_peer_last_rx_epoch(void);
extern "C" uint32_t ports_get_epoch_time(void);

#ifndef LINUX_BUILD
static TaskHandle_t audio_task;
void oai_send_audio_task(void *user_data) {
  while (1) {
    oai_send_audio(peer_connection);
  }
}

// Independent of every other task: if this stops printing, something has
// genuinely wedged the whole system (not just gone quiet because the mic is
// muted or the network is idle), which rules that distinction in or out.
static void oai_heartbeat_task(void *) {
  OaiPeerStats prev = {};
  unsigned prev_reads = 0, prev_skips = 0;
  while (1) {
    // ports_get_epoch_time() returns milliseconds despite its name.
    const uint32_t now_ms = ports_get_epoch_time();
    const uint32_t last_rx_ms = oai_peer_last_rx_epoch();
    const OaiPeerStats cur = oai_peer_stats;
    const uint32_t last_stun = cur.last_stun_resp_ms;
    const uint32_t last_consent = cur.last_consent_ok_ms;
    unsigned reads, skips;
    oai_mic_counters(&reads, &skips);
    ESP_LOGI(LOG_TAG, "Heartbeat: peer=%s, mic_muted=%d, last_rx=%ums ago, mic_reads=%u mic_muted_skips=%u",
             oai_peer_stage(), oai_is_mic_muted(), (unsigned)(last_rx_ms ? (now_ms - last_rx_ms) : 0),
             reads - prev_reads, skips - prev_skips);
    prev_reads = reads;
    prev_skips = skips;
    // Our own consent checks: consent_ok_ago climbing toward 30000 ms means
    // the server is not answering them, and will drop the session.
    ESP_LOGI(LOG_TAG, "Consent5s: sent=%u ok=%u consent_ok_ago=%ums",
             (unsigned)(cur.consent_sent - prev.consent_sent), (unsigned)(cur.consent_ok - prev.consent_ok),
             (unsigned)(last_consent ? (now_ms - last_consent) : 0));
    // Deltas over the last 5 s. stun_ok_ago is the time since we last
    // answered a consent check; the peer gives up after 30 s without one.
    ESP_LOGI(LOG_TAG, "Net5s: udp=%u backlog=%u stun_req=%u bad=%u ok=%u fail=%u stun_ok_ago=%ums dtls_pend=%u sctp_ooo=%u sctp_full=%u",
             (unsigned)(cur.udp_rx - prev.udp_rx), (unsigned)(cur.rx_backlog - prev.rx_backlog),
             (unsigned)(cur.stun_req - prev.stun_req), (unsigned)(cur.stun_req_bad - prev.stun_req_bad),
             (unsigned)(cur.stun_resp_ok - prev.stun_resp_ok), (unsigned)(cur.stun_resp_fail - prev.stun_resp_fail),
             (unsigned)(last_stun ? (now_ms - last_stun) : 0),
             (unsigned)(cur.dtls_pending - prev.dtls_pending),
             (unsigned)(cur.sctp_out_of_order - prev.sctp_out_of_order),
             (unsigned)(cur.sctp_reorder_full - prev.sctp_reorder_full));
    prev = cur;
    vTaskDelay(pdMS_TO_TICKS(5000));
  }
}

void oai_start_heartbeat(void) {
  xTaskCreate(oai_heartbeat_task, "heartbeat", 4096, NULL, 5, NULL);
}
#else
void oai_start_heartbeat(void) {}
#endif

// msg is not guaranteed to be NUL-terminated, so scan within len explicitly.
static bool oai_msg_contains(const char *msg, size_t len, const char *needle) {
  const size_t needle_len = strlen(needle);
  if (len < needle_len) return false;
  for (size_t i = 0; i + needle_len <= len; ++i) {
    if (memcmp(msg + i, needle, needle_len) == 0) return true;
  }
  return false;
}

static void oai_ondatachannel_onmessage_task(char *msg, size_t len,
                                             void *userdata, uint16_t sid) {
#ifdef LOG_DATACHANNEL_MESSAGES
  ESP_LOGI(LOG_TAG, "DataChannel Message: %.*s", (int)len, msg);
#endif
  // No AEC on this board: stop sending mic audio while the speaker plays the
  // response, so the server doesn't hear (and react to) its own output.
  if (oai_msg_contains(msg, len, "\"type\":\"output_audio_buffer.started\"")) {
    oai_set_speaker_active(true);
  } else if (oai_msg_contains(msg, len, "\"type\":\"output_audio_buffer.stopped\"") ||
             oai_msg_contains(msg, len, "\"type\":\"output_audio_buffer.cleared\"")) {
    oai_set_speaker_active(false);
  }
}

static void oai_ondatachannel_onopen_task(void *userdata) {
  if (peer_connection_create_datachannel(peer_connection, DATA_CHANNEL_RELIABLE,
                                         0, 0, (char *)"oai-events",
                                         (char *)"") != -1) {
    ESP_LOGI(LOG_TAG, "DataChannel created");
    peer_connection_datachannel_send(peer_connection, (char *)GREETING,
                                     strlen(GREETING));
  } else {
    ESP_LOGE(LOG_TAG, "Failed to create DataChannel");
  }
}

static void oai_ondatachannel_onclose_task(void *userdata) {
  // The ICE/DTLS/SRTP transport can outlive the SCTP association, so this
  // can fire while PeerConnectionState still reports "completed": once the
  // DataChannel is gone, no further turn-taking events will ever arrive.
  ESP_LOGW(LOG_TAG, "DataChannel closed (SCTP aborted); conversation will not continue - reset the board to retry");
}

static void oai_onconnectionstatechange_task(PeerConnectionState state,
                                             void *user_data) {
  ESP_LOGI(LOG_TAG, "PeerConnectionState: %s",
           peer_connection_state_to_string(state));

  if (state == PEER_CONNECTION_DISCONNECTED ||
      state == PEER_CONNECTION_CLOSED) {
    ESP_LOGW(LOG_TAG, "Realtime connection closed; reset the board to retry");
  } else if (state == PEER_CONNECTION_COMPLETED) {
#ifndef LINUX_BUILD
    if (!audio_task) {
      oai_init_audio_encoder();
      // The Opus encoder needs ~22 KB of stack. A contiguous 32 KB block of
      // internal RAM is often unavailable by now (Wi-Fi, queues, SCTP
      // buffers), and with NDEBUG ESP_ERROR_CHECK is a no-op, so a failed
      // xTaskCreate used to leave the board silently without a microphone.
      // The stack goes to PSRAM instead (FREERTOS_TASK_CREATE_ALLOW_EXT_MEM
      // is enabled); this task does no flash access, so that is safe.
      const BaseType_t created = xTaskCreatePinnedToCoreWithCaps(
          oai_send_audio_task, "audio_capture", 32768, NULL, 5, &audio_task, 1,
          MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
      if (created != pdPASS) {
        audio_task = NULL;
        ESP_LOGE(LOG_TAG, "Microphone task NOT created: internal largest free block=%u, PSRAM free=%u",
                 (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL),
                 (unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM));
      } else {
        ESP_LOGI(LOG_TAG, "Microphone task started (stack in PSRAM)");
      }
    }
#endif
  }
}

static void oai_on_icecandidate_task(char *description, void *user_data) {
  char *answer = static_cast<char *>(calloc(MAX_HTTP_OUTPUT_BUFFER, 1));
  if (!answer) {
    ESP_LOGE(LOG_TAG, "Not enough memory for Realtime answer");
    return;
  }
  if (oai_http_request(description, answer)) {
    peer_connection_set_remote_description(peer_connection, answer);
  } else {
    ESP_LOGE(LOG_TAG, "Realtime connection stopped; reset the board to retry");
  }
  free(answer);
}

void oai_webrtc() {
  PeerConfiguration peer_connection_config = {
      .ice_servers = {},
      .audio_codec = CODEC_OPUS,
      .video_codec = CODEC_NONE,
      .datachannel = DATA_CHANNEL_STRING,
      .onaudiotrack = [](uint8_t *data, size_t size, void *userdata) -> void {
#ifndef LINUX_BUILD
        oai_audio_decode(data, size);
#endif
      },
      .onvideotrack = NULL,
      .on_request_keyframe = NULL,
      .user_data = NULL,
  };

  peer_connection = peer_connection_create(&peer_connection_config);
  if (peer_connection == NULL) {
    ESP_LOGE(LOG_TAG, "Failed to create peer connection");
#ifndef LINUX_BUILD
    esp_restart();
#endif
  }

  peer_connection_oniceconnectionstatechange(peer_connection,
                                             oai_onconnectionstatechange_task);
  peer_connection_onicecandidate(peer_connection, oai_on_icecandidate_task);
  peer_connection_ondatachannel(peer_connection,
                                oai_ondatachannel_onmessage_task,
                                oai_ondatachannel_onopen_task,
                                oai_ondatachannel_onclose_task);

  peer_connection_create_offer(peer_connection);

  while (1) {
#ifndef LINUX_BUILD
    oai_drain_audio(peer_connection);
#endif
    peer_connection_loop(peer_connection);
    vTaskDelay(1);
    vTaskDelay(pdMS_TO_TICKS(TICK_INTERVAL));
  }
}
