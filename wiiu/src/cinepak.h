/*
 * cinepak.h - Standalone portable Cinepak ("cvid") video decoder.
 *
 * Self-contained: no ffmpeg, no external libraries. Only relies on
 * <stdint.h>/<string.h>/<stdlib.h>. Decodes a single Cinepak frame
 * (the payload of one AVI '00dc' chunk) into 32-bit RGBA8888.
 *
 * All multi-byte integers in the Cinepak bitstream are BIG-ENDIAN.
 */
#ifndef CINEPAK_H
#define CINEPAK_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct CinepakDecoder CinepakDecoder;

/* Create a decoder for frames of the given max dimensions.
 * Returns NULL on out-of-memory / invalid args. */
CinepakDecoder *cinepak_open(int width, int height);

/* Decode one Cinepak-encoded frame (the raw bytes of ONE frame, i.e. the
 * payload of one AVI '00dc' chunk) into 32-bit RGBA8888 (R,G,B,A byte order
 * in memory, A=255).
 *
 *   data/size  - the compressed frame bytes.
 *   out_rgba   - must point to at least out_pitch*height bytes.
 *   out_pitch  - row stride in bytes of out_rgba (>= width*4).
 *
 * The decoder keeps the previous frame internally so inter-coded (delta)
 * frames work: call it once per frame, in order. On an inter frame it
 * initializes the output from the previous frame and overwrites only the
 * changed macroblocks (skipped MBs keep previous content).
 *
 * Returns 0 on success, non-zero on a malformed/truncated frame. */
int cinepak_decode(CinepakDecoder *dec, const uint8_t *data, uint32_t size,
                   uint8_t *out_rgba, int out_pitch);

void cinepak_close(CinepakDecoder *dec);

#ifdef __cplusplus
}
#endif

#endif /* CINEPAK_H */
