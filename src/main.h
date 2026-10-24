#include <peer.h>

#define LOG_TAG "realtimeapi-sdk"
#define MAX_HTTP_OUTPUT_BUFFER 8192

void oai_wifi(void);
void oai_init_audio_capture(void);
void oai_audio_test_tone(void);
void oai_init_audio_decoder(void);
void oai_init_audio_encoder();
void oai_send_audio(PeerConnection *peer_connection);
void oai_drain_audio(PeerConnection *peer_connection);
void oai_set_speaker_active(bool active);
bool oai_is_mic_muted(void);
void oai_mic_counters(unsigned *reads, unsigned *muted_skips);
void oai_start_heartbeat(void);
void oai_audio_decode(uint8_t *data, size_t size);
void oai_webrtc();
bool oai_http_request(char *offer, char *answer);
