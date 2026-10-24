#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "buffer.h"

/* Owned by one peer loop. Audio producers use a FreeRTOS queue, not this ring.
 * Records stay contiguous; a zero length marks unused space before wrapping.
 */
Buffer* buffer_new(int size) {
  size &= ~3;
  if (size < 12) return NULL;
  Buffer* rb = (Buffer*)calloc(1, sizeof(Buffer));
  if (!rb) return NULL;
  rb->data = (uint8_t*)calloc(1, size);
  if (!rb->data) { free(rb); return NULL; }
  rb->size = size;
  return rb;
}

void buffer_clear(Buffer* rb) {
  if (rb) rb->head = rb->tail = 0;
}

void buffer_free(Buffer* rb) {
  if (rb) { free(rb->data); free(rb); }
}

int buffer_push_tail(Buffer* rb, const uint8_t* data, int size) {
  if (!rb || !data || size <= 0 || size > rb->size - 8) return -1;
  const int record = (size + 7) & ~3;
  const int available = (rb->size + rb->head - rb->tail - 1) % rb->size;
  if (record > available) return -1;
  int position = rb->tail;
  if (position >= rb->head && record > rb->size - position) {
    /* Both the unused end and the new record consume free space. */
    if (rb->size - position + record > available) return -1;
    const int marker = 0;
    memcpy(rb->data + position, &marker, sizeof(marker));
    position = 0;
  }
  memcpy(rb->data + position, &size, sizeof(size));
  memcpy(rb->data + position + 4, data, size);
  rb->tail = (position + record) % rb->size;
  return size;
}

uint8_t* buffer_peak_head(Buffer* rb, int* size) {
  if (!rb || !size || rb->head == rb->tail) return NULL;
  memcpy(size, rb->data + rb->head, sizeof(*size));
  if (*size == 0) {
    rb->head = 0;
    memcpy(size, rb->data, sizeof(*size));
  }
  if (*size <= 0 || *size > rb->size - rb->head - 4) return NULL;
  return rb->data + rb->head + 4;
}

void buffer_pop_head(Buffer* rb) {
  int size;
  if (!buffer_peak_head(rb, &size)) return;
  rb->head = (rb->head + ((size + 7) & ~3)) % rb->size;
}
