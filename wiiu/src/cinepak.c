/*
 * cinepak.c - Standalone portable Cinepak ("cvid") video decoder.
 *
 * Implements the Cinepak bitstream as documented by Tim Ferguson's public
 * "cvid" decoder. All multi-byte integers are BIG-ENDIAN. Output is
 * RGBA8888 (R,G,B,A byte order in memory, A=255).
 *
 * Frame header (10 bytes):
 *   [0]     flags (bit0 set => inter/delta frame)
 *   [1..3]  24-bit frame data length
 *   [4..5]  16-bit encoded width
 *   [6..7]  16-bit encoded height
 *   [8..9]  16-bit number of strips
 *
 * Strip header (12 bytes):
 *   [0..1]  strip ID (0x1000 intra, 0x1100 inter)
 *   [2..3]  strip size INCLUDING this header
 *   [4..5]  y0  [6..7] x0  [8..9] y1  [10..11] x1
 *   Strips stack vertically; the real top of strip N is the previous
 *   strip's bottom. Codebooks PERSIST across strips unless redefined.
 *
 * Chunk header (4 bytes):
 *   [0..1]  chunk ID
 *   [2..3]  chunk size INCLUDING this header
 *
 * Codebook entry -> a 2x2 luma block (y0,y1,y2,y3) plus shared signed
 * chroma (u,v). YUV->RGB full-range BT.601:
 *   R = clamp(Y + ((91881  * v) >> 16))
 *   G = clamp(Y - ((22554  * u + 46802 * v) >> 16))
 *   B = clamp(Y + ((116130 * u) >> 16))
 */
#include "cinepak.h"

#include <stdlib.h>
#include <string.h>

/* A codebook entry pre-expanded to 4 RGBA pixels (one per luma sample),
 * ready to be blitted. Layout: pix[0]=top-left, pix[1]=top-right,
 * pix[2]=bottom-left, pix[3]=bottom-right. Each is 4 bytes RGBA. */
typedef struct {
    uint8_t pix[4][4];
} cvid_codebook;

struct CinepakDecoder {
    int width;   /* max frame width  (allocation / clip bound) */
    int height;  /* max frame height (allocation / clip bound) */

    /* Persisted codebooks (per Cinepak there is one V1 and one V4 book).
     * They persist across strips within a frame and are not required to be
     * re-sent on inter frames, so we keep them in the decoder. */
    cvid_codebook v1[256];
    cvid_codebook v4[256];

    /* Previous decoded frame, kept as RGBA for inter prediction. */
    uint8_t *prev;   /* width*height*4 bytes, tightly packed (pitch=width*4) */
};

/* ---- clamping helper ---- */
static inline uint8_t clamp_u8(int v)
{
    if (v < 0)   return 0;
    if (v > 255) return 255;
    return (uint8_t)v;
}

/* ---- big-endian scalar reads (bounds are checked by the caller) ---- */
static inline uint16_t rd_be16(const uint8_t *p)
{
    return (uint16_t)((p[0] << 8) | p[1]);
}

/*
 * Expand a raw codebook entry into 4 RGBA pixels.
 *   y0,y1,y2,y3 : unsigned luma
 *   u,v         : signed chroma (already in -128..127 range)
 */
static void cb_make(cvid_codebook *cb, int y0, int y1, int y2, int y3,
                    int u, int v)
{
    int r = ( 91881 * v) >> 16;
    int g = (22554 * u + 46802 * v) >> 16;   /* subtracted from Y below */
    int b = (116130 * u) >> 16;
    const int ys[4] = { y0, y1, y2, y3 };
    int i;
    for (i = 0; i < 4; i++) {
        int y = ys[i];
        cb->pix[i][0] = clamp_u8(y + r);
        cb->pix[i][1] = clamp_u8(y - g);
        cb->pix[i][2] = clamp_u8(y + b);
        cb->pix[i][3] = 255;
    }
}

/*
 * Load / update a codebook from a chunk.
 *   cb        : target book (v1 or v4)
 *   selective : non-zero for the *_UPDATE chunk variants (0x2100/2300/2500/2700)
 *   bpe       : bytes per entry present in the stream (6 = with chroma,
 *               4 = grayscale, chroma forced to 0)
 *   data/size : chunk payload (already past the 4-byte chunk header)
 *
 * For selective updates a flag bitstream (32-bit BE words, MSB-first) chooses
 * which of the 256 entries are refreshed.
 */
static void read_codebook(cvid_codebook *cb, int selective, int bpe,
                          const uint8_t *data, uint32_t size)
{
    uint32_t pos = 0;
    uint32_t flags = 0;
    int flagbits = 0;   /* remaining valid bits in `flags` (MSB-first) */
    int i;

    for (i = 0; i < 256; i++) {
        int update = 1;

        if (selective) {
            if (flagbits == 0) {
                if (pos + 4 > size)
                    return;                 /* out of data: stop */
                flags = ((uint32_t)data[pos]   << 24) |
                        ((uint32_t)data[pos+1] << 16) |
                        ((uint32_t)data[pos+2] <<  8) |
                        ((uint32_t)data[pos+3]);
                pos += 4;
                flagbits = 32;
            }
            update = (flags & 0x80000000u) ? 1 : 0;
            flags <<= 1;
            flagbits--;
        }

        if (!update)
            continue;

        if (pos + (uint32_t)bpe > size)
            return;                         /* truncated: stop */

        if (bpe == 6) {
            int y0 = data[pos+0];
            int y1 = data[pos+1];
            int y2 = data[pos+2];
            int y3 = data[pos+3];
            int u  = (int8_t)data[pos+4];   /* signed chroma */
            int v  = (int8_t)data[pos+5];
            cb_make(&cb[i], y0, y1, y2, y3, u, v);
        } else {                            /* bpe == 4, grayscale */
            int y0 = data[pos+0];
            int y1 = data[pos+1];
            int y2 = data[pos+2];
            int y3 = data[pos+3];
            cb_make(&cb[i], y0, y1, y2, y3, 0, 0);
        }
        pos += (uint32_t)bpe;
    }
}

/*
 * Blit a codebook entry's 2x2 luma block, upsampled to a `scale`x`scale`
 * region, at output pixel (px,py). Used for both V1 (scale=2, fills a 4x4
 * MB where each entry pixel becomes a 2x2 quadrant) and V4 (scale=1, each
 * entry pixel is a single output pixel filling one 2x2 sub-block corner).
 *
 * Writes are clipped to [0,w) x [0,h).
 */
static void blit_entry(const cvid_codebook *cb, uint8_t *out, int pitch,
                       int w, int h, int px, int py, int scale)
{
    int q;
    for (q = 0; q < 4; q++) {
        int qx = (q & 1) * scale;   /* quadrant x offset */
        int qy = (q >> 1) * scale;  /* quadrant y offset */
        const uint8_t *src = cb->pix[q];
        int dy;
        for (dy = 0; dy < scale; dy++) {
            int y = py + qy + dy;
            if (y < 0 || y >= h)
                continue;
            uint8_t *row = out + (uint32_t)y * pitch;
            int dx;
            for (dx = 0; dx < scale; dx++) {
                int x = px + qx + dx;
                if (x < 0 || x >= w)
                    continue;
                uint8_t *d = row + x * 4;
                d[0] = src[0];
                d[1] = src[1];
                d[2] = src[2];
                d[3] = src[3];
            }
        }
    }
}

/*
 * Decode one V1 macroblock at (mbx,mby): a single index into the V1 book,
 * upsampled so each of the 4 entry pixels fills a 2x2 quadrant of the 4x4 MB.
 */
static void decode_v1(const cvid_codebook *v1book, uint8_t idx,
                      uint8_t *out, int pitch, int w, int h, int mbx, int mby)
{
    blit_entry(&v1book[idx], out, pitch, w, h, mbx, mby, 2);
}

/*
 * Decode one V4 macroblock: 4 indices i0..i3 into the V4 book. Each entry's
 * 2x2 fills one 2x2 sub-block of the MB:
 *   i0 -> top-left,  i1 -> top-right,  i2 -> bottom-left,  i3 -> bottom-right.
 * Within each sub-block the entry pixels map 1:1 (scale=1).
 */
static void decode_v4(const cvid_codebook *v4book, const uint8_t idx[4],
                      uint8_t *out, int pitch, int w, int h, int mbx, int mby)
{
    blit_entry(&v4book[idx[0]], out, pitch, w, h, mbx,     mby,     1);
    blit_entry(&v4book[idx[1]], out, pitch, w, h, mbx + 2, mby,     1);
    blit_entry(&v4book[idx[2]], out, pitch, w, h, mbx,     mby + 2, 1);
    blit_entry(&v4book[idx[3]], out, pitch, w, h, mbx + 2, mby + 2, 1);
}

/*
 * A bit/byte reader over a chunk's data. Per the Cinepak format the flag
 * bitstream and the index/codebook bytes are INTERLEAVED and pulled from the
 * SAME advancing pointer: when a flag bit is needed and none remain, the next
 * 4 bytes are read as a 32-bit BE word; the flag bit is the current MSB.
 * Index bytes are read from the same pointer, positioned AFTER whatever flag
 * words have already been consumed.
 */
typedef struct {
    const uint8_t *data;
    uint32_t size;
    uint32_t pos;
    uint32_t flags;    /* current flag accumulator (MSB = next bit) */
    int flagbits;      /* valid bits remaining in `flags` */
    int error;         /* set on any overrun */
} bitreader;

static void br_init(bitreader *br, const uint8_t *data, uint32_t size)
{
    br->data = data;
    br->size = size;
    br->pos = 0;
    br->flags = 0;
    br->flagbits = 0;
    br->error = 0;
}

/* Get one flag bit (MSB-first from 32-bit BE words). */
static int br_flag(bitreader *br)
{
    int bit;
    if (br->flagbits == 0) {
        if (br->pos + 4 > br->size) {
            br->error = 1;
            return 0;
        }
        br->flags = ((uint32_t)br->data[br->pos]   << 24) |
                    ((uint32_t)br->data[br->pos+1] << 16) |
                    ((uint32_t)br->data[br->pos+2] <<  8) |
                    ((uint32_t)br->data[br->pos+3]);
        br->pos += 4;
        br->flagbits = 32;
    }
    bit = (br->flags & 0x80000000u) ? 1 : 0;
    br->flags <<= 1;
    br->flagbits--;
    return bit;
}

/* Get one data (index) byte from the same advancing pointer. */
static uint8_t br_byte(bitreader *br)
{
    if (br->pos + 1 > br->size) {
        br->error = 1;
        return 0;
    }
    return br->data[br->pos++];
}

/*
 * Decode an image-vectors chunk.
 *   mode : 0 = intra (0x3000)         - per-MB flag: 0=V1, 1=V4
 *          1 = inter (0x3100)         - per-MB flag: 0=skip, 1=>read another
 *                                       flag (0=V1,1=V4)
 *          2 = intra V1-only (0x3200) - no flags, every MB is V1
 *
 * The strip rectangle is [x0,x1) x [y0,y1). MBs are 4x4, left-to-right then
 * top-to-bottom. Output writes are clipped to the frame's width/height.
 */
static void decode_vectors(CinepakDecoder *dec, int mode,
                           const uint8_t *data, uint32_t size,
                           uint8_t *out, int pitch, int w, int h,
                           int x0, int y0, int x1, int y1)
{
    bitreader br;
    int mbx, mby;

    br_init(&br, data, size);

    for (mby = y0; mby < y1; mby += 4) {
        for (mbx = x0; mbx < x1; mbx += 4) {
            int use_v4;

            if (mode == 2) {
                /* V1-only: every MB is a single V1 index. */
                uint8_t idx = br_byte(&br);
                if (br.error) return;
                decode_v1(dec->v1, idx, out, pitch, w, h, mbx, mby);
                continue;
            }

            if (mode == 1) {
                /* Inter: first flag chooses skip vs coded. */
                int coded = br_flag(&br);
                if (br.error) return;
                if (!coded)
                    continue;               /* skip: keep previous pixels */
                use_v4 = br_flag(&br);       /* second flag: 0=V1, 1=V4 */
                if (br.error) return;
            } else {
                /* Intra: one flag, 0=V1, 1=V4. */
                use_v4 = br_flag(&br);
                if (br.error) return;
            }

            if (use_v4) {
                uint8_t idx[4];
                idx[0] = br_byte(&br);
                idx[1] = br_byte(&br);
                idx[2] = br_byte(&br);
                idx[3] = br_byte(&br);
                if (br.error) return;
                decode_v4(dec->v4, idx, out, pitch, w, h, mbx, mby);
            } else {
                uint8_t idx = br_byte(&br);
                if (br.error) return;
                decode_v1(dec->v1, idx, out, pitch, w, h, mbx, mby);
            }
        }
    }
}

/*
 * Decode a single strip. `sdata`/`ssize` is the strip payload AFTER the
 * 12-byte strip header. (x0,y0,x1,y1) is the strip's real output rectangle
 * (already offset for vertical stacking). Codebooks are NOT cleared here so
 * they persist from previous strips.
 */
static void decode_strip(CinepakDecoder *dec, const uint8_t *sdata,
                         uint32_t ssize, uint8_t *out, int pitch,
                         int w, int h, int x0, int y0, int x1, int y1)
{
    uint32_t pos = 0;

    while (pos + 4 <= ssize) {
        uint16_t cid = rd_be16(sdata + pos);
        uint32_t clen = rd_be16(sdata + pos + 2);
        const uint8_t *cdata;
        uint32_t cpay;

        if (clen < 4)
            break;                          /* malformed chunk length */
        if (pos + clen > ssize)
            clen = ssize - pos;             /* clamp to strip bounds */

        cdata = sdata + pos + 4;
        cpay  = clen - 4;

        switch (cid) {
        /* ---- Codebook chunks ---- */
        case 0x2000: read_codebook(dec->v4, 0, 6, cdata, cpay); break; /* V4 full */
        case 0x2100: read_codebook(dec->v4, 1, 6, cdata, cpay); break; /* V4 update */
        case 0x2200: read_codebook(dec->v1, 0, 6, cdata, cpay); break; /* V1 full */
        case 0x2300: read_codebook(dec->v1, 1, 6, cdata, cpay); break; /* V1 update */
        case 0x2400: read_codebook(dec->v4, 0, 4, cdata, cpay); break; /* V4 gray */
        case 0x2500: read_codebook(dec->v4, 1, 4, cdata, cpay); break; /* V4 gray update */
        case 0x2600: read_codebook(dec->v1, 0, 4, cdata, cpay); break; /* V1 gray */
        case 0x2700: read_codebook(dec->v1, 1, 4, cdata, cpay); break; /* V1 gray update */

        /* ---- Image-vector chunks ---- */
        case 0x3000: /* intra */
            decode_vectors(dec, 0, cdata, cpay, out, pitch, w, h, x0, y0, x1, y1);
            break;
        case 0x3100: /* inter */
            decode_vectors(dec, 1, cdata, cpay, out, pitch, w, h, x0, y0, x1, y1);
            break;
        case 0x3200: /* intra, V1-only */
            decode_vectors(dec, 2, cdata, cpay, out, pitch, w, h, x0, y0, x1, y1);
            break;

        default:
            /* Unknown chunk: skip it. */
            break;
        }

        pos += clen;
    }
}

CinepakDecoder *cinepak_open(int width, int height)
{
    CinepakDecoder *dec;

    if (width <= 0 || height <= 0)
        return NULL;

    dec = (CinepakDecoder *)calloc(1, sizeof(*dec));
    if (!dec)
        return NULL;

    dec->width = width;
    dec->height = height;
    dec->prev = (uint8_t *)calloc((size_t)width * height, 4);
    if (!dec->prev) {
        free(dec);
        return NULL;
    }

    return dec;
}

void cinepak_close(CinepakDecoder *dec)
{
    if (!dec)
        return;
    free(dec->prev);
    free(dec);
}

int cinepak_decode(CinepakDecoder *dec, const uint8_t *data, uint32_t size,
                   uint8_t *out_rgba, int out_pitch)
{
    int flags;
    int fw, fh, nstrips;
    int inter;
    int w, h;
    int internal_pitch;
    uint32_t pos;
    int s;
    int strip_top;   /* running vertical origin (previous strip's bottom) */

    if (!dec || !data || !out_rgba)
        return 1;
    if (out_pitch < dec->width * 4)
        return 1;
    if (size < 10)
        return 1;

    /* ---- Frame header (10 bytes, big-endian) ---- */
    flags   = data[0];
    /* data[1..3] = 24-bit length (not strictly needed; we trust `size`). */
    fw      = rd_be16(data + 4);
    fh      = rd_be16(data + 6);
    nstrips = rd_be16(data + 8);
    inter   = (flags & 0x01) ? 1 : 0;

    /* Clip encoded dimensions to what we allocated for. */
    w = fw;
    h = fh;
    if (w > dec->width)  w = dec->width;
    if (h > dec->height) h = dec->height;
    if (w <= 0 || h <= 0)
        return 1;

    internal_pitch = dec->width * 4;

    /* For an inter frame, seed the output from the previous frame so that
     * skipped macroblocks keep their previous pixels. For an intra frame the
     * output is fully overwritten by coded MBs (strips cover the frame). */
    if (inter) {
        int y;
        for (y = 0; y < h; y++) {
            memcpy(out_rgba + (uint32_t)y * out_pitch,
                   dec->prev + (uint32_t)y * internal_pitch,
                   (size_t)w * 4);
        }
    }

    /* ---- Strips ---- */
    pos = 10;
    strip_top = 0;
    for (s = 0; s < nstrips; s++) {
        uint16_t sid;
        uint32_t slen;
        int sy0, sx0, sy1, sx1;
        int rx0, ry0, rx1, ry1;
        int strip_h;

        if (pos + 12 > size)
            break;                          /* truncated */

        sid  = rd_be16(data + pos);
        slen = rd_be16(data + pos + 2);
        sy0  = rd_be16(data + pos + 4);
        sx0  = rd_be16(data + pos + 6);
        sy1  = rd_be16(data + pos + 8);
        sx1  = rd_be16(data + pos + 10);
        (void)sid;
        (void)sx0;
        (void)sx1;

        if (slen < 12)
            break;                          /* malformed */
        if (pos + slen > size)
            slen = size - pos;              /* clamp */

        /* Strips stack vertically: the real top is the previous strip's
         * bottom; the height is (y1 - y0). The x span is the full frame. */
        strip_h = sy1 - sy0;
        if (strip_h < 0)
            strip_h = 0;

        ry0 = strip_top;
        ry1 = strip_top + strip_h;
        rx0 = 0;
        rx1 = fw;                           /* full encoded width for MB grid */

        /* Clip the output rectangle to the (allocated) frame bounds. The MB
         * loop still walks the full encoded grid; blits clip per-pixel. */
        if (rx1 > fw) rx1 = fw;

        decode_strip(dec, data + pos + 12, slen - 12,
                     out_rgba, out_pitch, w, h,
                     rx0, ry0, rx1, ry1);

        strip_top = ry1;
        pos += slen;
    }

    /* Save this decoded frame as the previous frame (tightly packed). */
    {
        int y;
        for (y = 0; y < h; y++) {
            memcpy(dec->prev + (uint32_t)y * internal_pitch,
                   out_rgba + (uint32_t)y * out_pitch,
                   (size_t)w * 4);
        }
    }

    return 0;
}
