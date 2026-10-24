#ifndef OAI_PEER_STATS_H_
#define OAI_PEER_STATS_H_

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// Monotonic diagnostic counters, written only by the peer connection task
// and sampled by the heartbeat task. Counts and timestamps only: never
// packet contents, ICE credentials or message bodies.
typedef struct {
  uint32_t udp_rx;             // datagrams read from the UDP socket (incl. STUN)
  uint32_t rx_backlog;         // loop rounds that left another datagram waiting
  uint32_t stun_req;           // STUN binding requests received
  uint32_t stun_req_bad;       // requests that failed integrity validation
  uint32_t stun_resp_ok;       // binding responses sent successfully
  uint32_t stun_resp_fail;     // binding responses whose send failed
  uint32_t last_stun_resp_ms;  // ports_get_epoch_time() of last successful response
  uint32_t dtls_pending;       // DTLS reads that left records buffered in mbedTLS
  uint32_t sctp_out_of_order;  // DATA chunks ahead of the expected TSN
  uint32_t sctp_reorder_full;  // of those, dropped because every slot was taken
  uint32_t consent_sent;       // our periodic ICE consent checks (RFC 7675)
  uint32_t consent_ok;         // valid binding responses to our checks
  uint32_t last_consent_ok_ms; // ports_get_epoch_time() of the last valid one
} OaiPeerStats;

extern OaiPeerStats oai_peer_stats;

#ifdef __cplusplus
}
#endif

#endif  // OAI_PEER_STATS_H_
