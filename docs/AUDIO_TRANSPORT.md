# Audio transport fixes

The peer component compiles local overrides for buffer.c and rtp.c instead of
editing the libpeer Git submodule. All other peer sources remain upstream.
The ring uses aligned contiguous records and an explicit wrap marker, avoiding
out-of-bounds length writes when a wrapped tail sits within the final 3 bytes.
A single task owns libpeer. The microphone task transfers encoded 20 ms frames
through a bounded FreeRTOS queue. A separate task writes decoded samples to I2S,
so slow playback does not block the network loop. Under sustained overload a
frame is dropped, with rate-limited warnings, rather than unbounded buffering.
Opus receive capacity covers 120 ms at the configured sample rate and includes
both output channels. I2S writes use the returned decoded sample count.
RTP payload parsing skips CSRCs, extension headers, and padding before Opus.

Diagnostics: Microphone active: peak=...; First incoming audio decoded (...);
First audio frame written to speaker (...). Values show amplitude/counts only,
not recorded audio or transcripts. These messages verify software flow;
hearing actual audio still requires correct speaker hardware and board wiring.

Buffer regressions: run tests/buffer/run.py with Python 3 and host gcc.
RTP regressions: run tests/rtp/run.py with Python 3 and host gcc.
No ESP32, credentials, API calls, or network are required for those tests.

DTLS receive consumes each datagram once; application reads yield on WANT_READ/
WANT_WRITE so capture draining cannot freeze on an empty or duplicate record.
Run tests/dtls/run.py for the offline regression tests.
DFR1154 uses 16 kHz PCM, a 2.048 MHz PDM microphone clock, and Philips I2S output.
A quiet 150 ms startup tone checks the speaker without a network session.
