#include <driver/i2s.h>
#include <opus.h>
#include <esp_log.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/queue.h>
#include <stdlib.h>

#include "main.h"
extern "C" const char* oai_peer_stage(void);
#define USE_DFR1154 1

#if USE_DFR1154
#include "driver/i2s_pdm.h"
#include "driver/i2s_std.h"
#define OPUS_OUT_BUFFER_SIZE 1276  // 1276 bytes is recommended by opus_encode
#define SAMPLE_RATE 16000
#define BUFFER_SAMPLES 320

#define MCLK_PIN 0
#define DAC_BCLK_PIN 45
#define DAC_LRCLK_PIN 46
#define DAC_DATA_PIN 42

#define PDM_CLK_PIN 38
#define PDM_DATA_PIN 39
i2s_chan_handle_t rx_chan;        // I2S rx channel handler
i2s_chan_handle_t tx_chan;        // I2S tx channel handler

#else
#define OPUS_OUT_BUFFER_SIZE 1276  // 1276 bytes is recommended by opus_encode
#define SAMPLE_RATE 8000
#define BUFFER_SAMPLES 320

#define MCLK_PIN 0
#define DAC_BCLK_PIN 15
#define DAC_LRCLK_PIN 16
#define DAC_DATA_PIN 17
#define ADC_BCLK_PIN 38
#define ADC_LRCLK_PIN 39
#define ADC_DATA_PIN 40
#endif

#define OPUS_ENCODER_BITRATE 30000
#define OPUS_ENCODER_COMPLEXITY 0


#if USE_DFR1154
static void probe_microphone(i2s_pdm_rx_slot_config_t slot_cfg) {
  // Try both sampling edges: inactive PDM slots produce a constant DC value.
  int16_t samples[320];
  int spans[2] = {0, 0};
  const i2s_pdm_slot_mask_t masks[2] = {I2S_PDM_SLOT_LEFT, I2S_PDM_SLOT_RIGHT};
  for (int candidate = 0; candidate < 2; ++candidate) {
    ESP_ERROR_CHECK(i2s_channel_disable(rx_chan));
    slot_cfg.slot_mask = masks[candidate];
    ESP_ERROR_CHECK(i2s_channel_reconfig_pdm_rx_slot(rx_chan, &slot_cfg));
    ESP_ERROR_CHECK(i2s_channel_enable(rx_chan));
    int low = 32767, high = -32768;
    bool complete = true;
    for (int frame = 0; frame < 12; ++frame) {
      size_t bytes = 0;
      esp_err_t err = i2s_channel_read(rx_chan, samples, sizeof(samples), &bytes, 500);
      if (err != ESP_OK || bytes != sizeof(samples)) { complete = false; break; }
      // Let the hardware filter settle before measuring.
      if (frame < 6) continue;
      for (int i = 0; i < 320; ++i) {
        if (samples[i] < low) low = samples[i];
        if (samples[i] > high) high = samples[i];
      }
    }
    spans[candidate] = complete ? high - low : 0;
    ESP_LOGI(LOG_TAG, "Microphone probe %s: range=%d..%d, span=%d, complete=%d",
        candidate ? "right" : "left", low, high, spans[candidate], complete);
  }
  // Prefer the documented left slot unless it has no changing signal.
  const int selected = spans[0] <= 4 && spans[1] > 4 ? 1 : 0;
  ESP_ERROR_CHECK(i2s_channel_disable(rx_chan));
  slot_cfg.slot_mask = masks[selected];
  ESP_ERROR_CHECK(i2s_channel_reconfig_pdm_rx_slot(rx_chan, &slot_cfg));
  ESP_ERROR_CHECK(i2s_channel_enable(rx_chan));
  ESP_LOGI(LOG_TAG, "Microphone selected: %s", selected ? "right" : "left");
  if (spans[selected] <= 4)
    ESP_LOGW(LOG_TAG, "Microphone has no changing signal on either slot; capture is not verified");
}
#endif

void oai_init_audio_capture() {
  // Initialize the mic (I2S0) before the speaker (I2S1): DFRobot's own
  // DFR1154 example does the same, and configuring I2S1 first was suspected
  // of disturbing I2S0's PDM clock generation (mic read a frozen signal).
#if USE_DFR1154
  i2s_chan_config_t rx_chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0, I2S_ROLE_MASTER);
  ESP_ERROR_CHECK(i2s_new_channel(&rx_chan_cfg, NULL, &rx_chan));
  i2s_pdm_rx_config_t pdm_rx_cfg = {
      .clk_cfg = I2S_PDM_RX_CLK_DEFAULT_CONFIG(SAMPLE_RATE),
      /* The data bit-width of PDM mode is fixed to 16 */
      .slot_cfg = I2S_PDM_RX_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_MONO),
      .gpio_cfg = {
          .clk = GPIO_NUM_38,
          .din = GPIO_NUM_39,
          .invert_flags = {
              .clk_inv = false,
          },
      },
  };
  // Keep the default I2S_PDM_DSR_8S downsample rate (1.024 MHz PDM clock at
  // 16 kHz) and the default PCM data format (matches DFRobot's own DFR1154
  // example exactly). RAW format needs clk_cfg.sample_rate_hz set to the raw
  // PDM bit clock (1.024-6.144 MHz), not 16000 - using it with the audio
  // sample rate fails i2s_channel_init_pdm_rx_mode outright.
  pdm_rx_cfg.slot_cfg.slot_mask = I2S_PDM_SLOT_LEFT;
  ESP_ERROR_CHECK(i2s_channel_init_pdm_rx_mode(rx_chan, &pdm_rx_cfg));
  ESP_ERROR_CHECK(i2s_channel_enable(rx_chan));
  probe_microphone(pdm_rx_cfg.slot_cfg);
#else
  i2s_config_t i2s_config_in = {
      .mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_RX),
      .sample_rate = SAMPLE_RATE,
      .bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT,
      .channel_format = I2S_CHANNEL_FMT_ONLY_LEFT,
      .communication_format = I2S_COMM_FORMAT_I2S_MSB,
      .intr_alloc_flags = ESP_INTR_FLAG_LEVEL1,
      .dma_buf_count = 8,
      .dma_buf_len = BUFFER_SAMPLES,
      .use_apll = 1,
  };

  if (i2s_driver_install(I2S_NUM_1, &i2s_config_in, 0, NULL) != ESP_OK) {
    printf("Failed to configure I2S driver for audio input");
    return;
  }

  i2s_pin_config_t pin_config_in = {
      .mck_io_num = MCLK_PIN,
      .bck_io_num = ADC_BCLK_PIN,
      .ws_io_num = ADC_LRCLK_PIN,
      .data_out_num = I2S_PIN_NO_CHANGE,
      .data_in_num = ADC_DATA_PIN,
  };
  if (i2s_set_pin(I2S_NUM_1, &pin_config_in) != ESP_OK) {
    printf("Failed to set I2S pins for audio input");
    return;
  }
#endif

#if USE_DFR1154
  // PDM RX requires I2S0 (configured above). Use I2S1 for the independent
  // speaker clock.
  i2s_chan_config_t tx_chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_1, I2S_ROLE_MASTER);
  // Send silence when idle instead of replaying stale DMA audio.
  tx_chan_cfg.auto_clear = true;
  ESP_ERROR_CHECK(i2s_new_channel(&tx_chan_cfg, &tx_chan, NULL));
  i2s_std_config_t tx_std_cfg={
    .clk_cfg  = I2S_STD_CLK_DEFAULT_CONFIG(SAMPLE_RATE),
    .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_STEREO),
    .gpio_cfg = {
      .mclk = GPIO_NUM_NC,
      .bclk = GPIO_NUM_45,
      .ws   = GPIO_NUM_46,
      .dout = GPIO_NUM_42,
      .din  = GPIO_NUM_NC,
      .invert_flags = {
        .mclk_inv = false,
        .bclk_inv = false,
        .ws_inv   = false,
      },
    }
  };
  ESP_ERROR_CHECK(i2s_channel_init_std_mode(tx_chan, &tx_std_cfg));
  ESP_ERROR_CHECK(i2s_channel_enable(tx_chan));
  ESP_LOGI(LOG_TAG, "Audio configured: microphone I2S0, speaker I2S1, 16000 Hz");
#else
  i2s_config_t i2s_config_out = {
      .mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_TX),
      .sample_rate = SAMPLE_RATE,
      .bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT,
      .channel_format = I2S_CHANNEL_FMT_RIGHT_LEFT,
      .communication_format = I2S_COMM_FORMAT_I2S_MSB,
      .intr_alloc_flags = ESP_INTR_FLAG_LEVEL1,
      .dma_buf_count = 8,
      .dma_buf_len = BUFFER_SAMPLES,
      .use_apll = 1,
      .tx_desc_auto_clear = true,
  };

  if (i2s_driver_install(I2S_NUM_0, &i2s_config_out, 0, NULL) != ESP_OK) {
    printf("Failed to configure I2S driver for audio output");
    return;
  }

  i2s_pin_config_t pin_config_out = {
      .mck_io_num = MCLK_PIN,
      .bck_io_num = DAC_BCLK_PIN,
      .ws_io_num = DAC_LRCLK_PIN,
      .data_out_num = DAC_DATA_PIN,
      .data_in_num = I2S_PIN_NO_CHANGE,
  };
  if (i2s_set_pin(I2S_NUM_0, &pin_config_out) != ESP_OK) {
    printf("Failed to set I2S pins for audio output");
    return;
  }
  i2s_zero_dma_buffer(I2S_NUM_0);
#endif
}

void oai_audio_test_tone() {
  // Quiet 150 ms triangle wave, before network/audio tasks start.
  opus_int16 pcm[160 * 2];
  int phase = 0;
  ESP_LOGI(LOG_TAG, "Speaker test tone starting");
  for (int block = 0; block < 15; ++block) {
    for (int i = 0; i < 160; ++i) {
      phase = (phase + 440) % SAMPLE_RATE;
      const int ramp = phase * 4000 / SAMPLE_RATE;
      const opus_int16 sample = ramp < 2000 ? ramp - 1000 : 3000 - ramp;
      pcm[i * 2] = pcm[i * 2 + 1] = sample;
    }
    size_t written = 0;
#if USE_DFR1154
    const esp_err_t err = i2s_channel_write(tx_chan, pcm, sizeof(pcm), &written, 500);
#else
    const esp_err_t err = i2s_write(I2S_NUM_0, pcm, sizeof(pcm), &written, pdMS_TO_TICKS(500));
#endif
    if (err != ESP_OK || written != sizeof(pcm)) {
      ESP_LOGW(LOG_TAG, "Speaker test failed: %s", esp_err_to_name(err));
      return;
    }
  }
  ESP_LOGI(LOG_TAG, "Speaker test tone sent");
}

// Frame sizes are samples PER CHANNEL; byte counts include every channel.
static constexpr int FRAME_SAMPLES = SAMPLE_RATE * 20 / 1000;
static constexpr int OUTPUT_CHANNELS = 2;
static constexpr int MAX_DECODE_SAMPLES = SAMPLE_RATE * 120 / 1000;
static constexpr int AUDIO_QUEUE_DEPTH = 8;

struct EncodedFrame {
  uint16_t length;
  uint8_t data[OPUS_OUT_BUFFER_SIZE];
};
struct PlaybackFrame {
  uint16_t samples;
  opus_int16 data[MAX_DECODE_SAMPLES * OUTPUT_CHANNELS];
};

static QueueHandle_t capture_queue;
static QueueHandle_t playback_queue;
static OpusDecoder *opus_decoder;
static OpusEncoder *opus_encoder;
static PlaybackFrame decoded_frame;
static opus_int16 encoder_input_buffer[FRAME_SAMPLES];
static EncodedFrame encoded_frame;
static unsigned captured_frames, dropped_frames;
static volatile unsigned drained_frames;
static unsigned decoded_frames, played_frames, playback_dropped;
static volatile int playback_last_peak = -1;
// No acoustic echo cancellation on this board: the mic otherwise picks up
// the speaker's own output and the server keeps responding to itself.
//
// The server's output_audio_buffer.started/stopped DataChannel events are
// NOT tightly synced to local playback: text and audio are streamed in
// parallel, so audible speech can start before "started" arrives, and
// decoded audio can still be sitting in playback_queue (up to
// AUDIO_QUEUE_DEPTH * 20 ms) after "stopped" arrives. Muting is therefore
// driven primarily off real local decode activity (updated on every
// decoded frame, with a trailing grace period), with the server events kept
// only as an earlier/extra signal, not the sole source of truth.
static volatile bool speaker_active = false;
static volatile TickType_t last_decode_tick = 0;
static constexpr TickType_t MIC_MUTE_GRACE_TICKS = pdMS_TO_TICKS(500);

void oai_set_speaker_active(bool active) {
  if (active != speaker_active) {
    ESP_LOGI(LOG_TAG, "Mic event-muted=%d", active);
  }
  speaker_active = active;
}

static bool oai_mic_should_be_muted() {
  return speaker_active ||
         (xTaskGetTickCount() - last_decode_tick) < MIC_MUTE_GRACE_TICKS;
}

bool oai_is_mic_muted(void) { return oai_mic_should_be_muted(); }

static volatile unsigned mic_reads, mic_muted_skips;
void oai_mic_counters(unsigned *reads, unsigned *muted_skips) {
  *reads = mic_reads;
  *muted_skips = mic_muted_skips;
}

static void audio_playback_task(void *) {
  auto *frame = static_cast<PlaybackFrame *>(malloc(sizeof(PlaybackFrame)));
  if (!frame) {
    ESP_LOGE(LOG_TAG, "Audio playback allocation failed");
    vTaskDelete(NULL);
    return;
  }
  bool first = true;
  bool warned = false;
  TickType_t last_warning = 0;
  while (true) {
    if (xQueueReceive(playback_queue, frame, portMAX_DELAY) != pdTRUE) continue;
    const size_t bytes = frame->samples * OUTPUT_CHANNELS * sizeof(opus_int16);
    size_t written = 0;
#if USE_DFR1154
    const esp_err_t err = i2s_channel_write(tx_chan, frame->data, bytes, &written, 500);
#else
    const esp_err_t err = i2s_write(I2S_NUM_0, frame->data, bytes, &written, pdMS_TO_TICKS(500));
#endif
    if (err == ESP_OK && written == bytes) {
      if (first) ESP_LOGI(LOG_TAG, "First audio frame written to speaker (%u bytes)", (unsigned)written);
      first = false;
      ++played_frames;
    } else if (!warned || xTaskGetTickCount() - last_warning >= pdMS_TO_TICKS(1000)) {
      ESP_LOGW(LOG_TAG, "Audio output incomplete: %s, %u/%u bytes", esp_err_to_name(err), (unsigned)written, (unsigned)bytes);
      last_warning = xTaskGetTickCount();
      warned = true;
    }
  }
}

void oai_init_audio_decoder() {
  int error;
  opus_decoder = opus_decoder_create(SAMPLE_RATE, OUTPUT_CHANNELS, &error);
  ESP_ERROR_CHECK(error == OPUS_OK && opus_decoder ? ESP_OK : ESP_FAIL);
  playback_queue = xQueueCreate(AUDIO_QUEUE_DEPTH, sizeof(PlaybackFrame));
  ESP_ERROR_CHECK(playback_queue ? ESP_OK : ESP_ERR_NO_MEM);
  ESP_ERROR_CHECK(xTaskCreate(audio_playback_task, "audio_playback", 4096, NULL, 5, NULL) == pdPASS ? ESP_OK : ESP_ERR_NO_MEM);
}

void oai_audio_decode(uint8_t *data, size_t size) {
  if (!opus_decoder || !data || !size) return;
  int samples = opus_decode(opus_decoder, data, size, decoded_frame.data, MAX_DECODE_SAMPLES, 0);
  static TickType_t last_warning = 0;
  static bool first = true;
  static bool warned = false;
  if (samples <= 0) {
    if (!warned || xTaskGetTickCount() - last_warning >= pdMS_TO_TICKS(1000)) {
      ESP_LOGW(LOG_TAG, "Opus decode failed: %s", opus_strerror(samples));
      last_warning = xTaskGetTickCount();
    }
    warned = true;
    return;
  }
  if (first) ESP_LOGI(LOG_TAG, "First incoming audio decoded (%d samples/channel)", samples);
  first = false;
  ++decoded_frames;
  int peak = 0;
  const int total = samples * OUTPUT_CHANNELS;
  for (int i = 0; i < total; ++i) {
    const int magnitude = decoded_frame.data[i] < 0 ? -decoded_frame.data[i] : decoded_frame.data[i];
    if (magnitude > peak) peak = magnitude;
  }
  // The server streams continuously (comfort noise/near-silence between
  // responses too), so only treat genuinely audible frames as "speaking" -
  // otherwise the mic would stay muted for the whole connection.
  constexpr int SPEECH_PEAK_THRESHOLD = 200;
  if (peak > SPEECH_PEAK_THRESHOLD) last_decode_tick = xTaskGetTickCount();
  playback_last_peak = peak;
  decoded_frame.samples = samples;
  if (xQueueSend(playback_queue, &decoded_frame, 0) != pdTRUE) {
    ++playback_dropped;
    if (xTaskGetTickCount() - last_warning >= pdMS_TO_TICKS(1000)) {
      ESP_LOGW(LOG_TAG, "Playback queue full; audio frame dropped");
      last_warning = xTaskGetTickCount();
    }
  }
  static TickType_t last_meter = 0;
  if (xTaskGetTickCount() - last_meter >= pdMS_TO_TICKS(5000)) {
    ESP_LOGI(LOG_TAG, "Speaker active: peak=%d, decoded=%u, played=%u, dropped=%u",
             playback_last_peak, decoded_frames, played_frames, playback_dropped);
    last_meter = xTaskGetTickCount();
  }
}

void oai_init_audio_encoder() {
  if (opus_encoder) return;
  int error;
  opus_encoder = opus_encoder_create(SAMPLE_RATE, 1, OPUS_APPLICATION_VOIP, &error);
  ESP_ERROR_CHECK(error == OPUS_OK && opus_encoder ? ESP_OK : ESP_FAIL);
  ESP_ERROR_CHECK(opus_encoder_ctl(opus_encoder, OPUS_SET_BITRATE(OPUS_ENCODER_BITRATE)) == OPUS_OK ? ESP_OK : ESP_FAIL);
  ESP_ERROR_CHECK(opus_encoder_ctl(opus_encoder, OPUS_SET_COMPLEXITY(OPUS_ENCODER_COMPLEXITY)) == OPUS_OK ? ESP_OK : ESP_FAIL);
  capture_queue = xQueueCreate(AUDIO_QUEUE_DEPTH, sizeof(EncodedFrame));
  ESP_ERROR_CHECK(capture_queue ? ESP_OK : ESP_ERR_NO_MEM);
}

void oai_send_audio(PeerConnection *) {
  // This producer never touches libpeer: its ringbuffers are not thread-safe.
  size_t bytes_read = 0;
#if USE_DFR1154
  esp_err_t err = i2s_channel_read(rx_chan, encoder_input_buffer, sizeof(encoder_input_buffer), &bytes_read, 1000);
#else
  esp_err_t err = i2s_read(I2S_NUM_1, encoder_input_buffer, sizeof(encoder_input_buffer), &bytes_read, pdMS_TO_TICKS(1000));
#endif
  static TickType_t last_warning = 0;
  if (err != ESP_OK || bytes_read != sizeof(encoder_input_buffer)) {
    if (xTaskGetTickCount() - last_warning >= pdMS_TO_TICKS(1000)) {
      ESP_LOGW(LOG_TAG, "Microphone read incomplete: %s, %u bytes", esp_err_to_name(err), (unsigned)bytes_read);
      last_warning = xTaskGetTickCount();
    }
    vTaskDelay(pdMS_TO_TICKS(20));
    return;
  }
  mic_reads = mic_reads + 1;
  if (oai_mic_should_be_muted()) {  // Don't let the mic hear the speaker (no AEC).
    mic_muted_skips = mic_muted_skips + 1;
    return;
  }
  ++captured_frames;
  int minimum = 32767, maximum = -32768;
  int peak = 0;
  for (int i = 0; i < FRAME_SAMPLES; ++i) {
    const int value = encoder_input_buffer[i];
    if (value < minimum) minimum = value;
    if (value > maximum) maximum = value;
    const int magnitude = value < 0 ? -value : value;
    if (magnitude > peak) peak = magnitude;
  }
  const int length = opus_encode(opus_encoder, encoder_input_buffer, FRAME_SAMPLES,
                                 encoded_frame.data, sizeof(encoded_frame.data));
  if (length <= 0) {
    if (xTaskGetTickCount() - last_warning >= pdMS_TO_TICKS(1000)) {
      ESP_LOGW(LOG_TAG, "Opus encode failed: %s", opus_strerror(length));
      last_warning = xTaskGetTickCount();
    }
    return;
  }
  encoded_frame.length = length;
  if (xQueueSend(capture_queue, &encoded_frame, 0) != pdTRUE) {
    ++dropped_frames;
    if (xTaskGetTickCount() - last_warning >= pdMS_TO_TICKS(1000)) {
    ESP_LOGW(LOG_TAG, "Capture queue full; microphone frame dropped");
    last_warning = xTaskGetTickCount();
    }
  }
  static TickType_t last_meter = 0;
  if (xTaskGetTickCount() - last_meter >= pdMS_TO_TICKS(5000)) {
    ESP_LOGI(LOG_TAG, "Microphone active: peak=%d, range=%d..%d, captured=%u, drained=%u, dropped=%u, stack_free_min=%u bytes, peer=%s", peak, minimum, maximum, captured_frames, drained_frames, dropped_frames, (unsigned)uxTaskGetStackHighWaterMark(NULL), oai_peer_stage());
    last_meter = xTaskGetTickCount();
  }
}

void oai_drain_audio(PeerConnection *pc) {
  // Called only by the same task that runs peer_connection_loop().
  if (!capture_queue || peer_connection_get_state(pc) != PEER_CONNECTION_COMPLETED) return;
  static EncodedFrame frame;
  if (xQueueReceive(capture_queue, &frame, 0) != pdTRUE) return;
  ++drained_frames;
  static TickType_t last_warning = 0;
  if (peer_connection_send_audio(pc, frame.data, frame.length) < 0 &&
      xTaskGetTickCount() - last_warning >= pdMS_TO_TICKS(1000)) {
    ESP_LOGW(LOG_TAG, "Outgoing audio ring full; frame dropped");
    last_warning = xTaskGetTickCount();
  }
}
