#ifndef PEER_RTP_PAYLOAD_H
#define PEER_RTP_PAYLOAD_H
#include <stddef.h>
#include <stdint.h>

/* RFC 3550: CSRCs, extension headers and padding are not codec payload. */
static int peer_rtp_payload(const uint8_t *packet, size_t size,
                            size_t *offset, size_t *length) {
  if (!packet || size < 12 || (packet[0] >> 6) != 2) return 0;
  size_t start = 12 + 4 * (packet[0] & 0x0f);
  if (start > size) return 0;
  if (packet[0] & 0x10) {
    if (size - start < 4) return 0;
    size_t words = ((size_t)packet[start + 2] << 8) | packet[start + 3];
    if (words > (size - start - 4) / 4) return 0;
    start += 4 + words * 4;
  }
  size_t end = size;
  if (packet[0] & 0x20) {
    size_t padding = packet[size - 1];
    if (!padding || padding > size - start) return 0;
    end -= padding;
  }
  if (end <= start) return 0;
  *offset = start;
  *length = end - start;
  return 1;
}
#endif
