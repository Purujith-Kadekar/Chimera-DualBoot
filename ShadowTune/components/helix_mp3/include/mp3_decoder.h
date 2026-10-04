#pragma once
// ═══════════════════════════════════════════════════════════════════════════════
//  mp3_decoder.h — Helix MP3 decoder API (public wrapper header)
// ═══════════════════════════════════════════════════════════════════════════════
//  This header declares the API used by mp3_player.cpp for MP3 decoding.
//  The implementation is the real RealNetworks/Helix fixed-point MP3 decoder
//  (as used in ESP8266Audio / ESP-ADF), vendored in components/helix_mp3/src/.
//  These declarations are kept byte-for-byte compatible with the upstream
//  mp3dec.h public API so the two can link together without redefinition
//  conflicts.
//
//  API reference:
//    HMP3Decoder  — opaque handle (void*)
//    MP3InitDecoder()      — allocate and return a new decoder
//    MP3FreeDecoder()      — free a decoder
//    MP3Decode()           — decode one frame
//    MP3GetLastFrameInfo() — get sample rate, channels, bitrate, etc.
//
//  Error codes:
//    ERR_MP3_NONE             =  0  (success)
//    ERR_MP3_INDATA_UNDERFLOW = -1  (not enough data — refill and retry)
// ═══════════════════════════════════════════════════════════════════════════════
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// ── Opaque decoder handle ──────────────────────────────────────────────────
typedef void* HMP3Decoder;

// ── Frame info (returned by MP3GetLastFrameInfo) ───────────────────────────
//  NOTE: field is "bitrate" (lowercase r) to match the upstream Helix struct.
typedef struct {
    int bitrate;           // bitrate in bps (e.g., 128000)
    int nChans;             // number of channels (1=mono, 2=stereo)
    int samprate;           // sample rate in Hz (e.g., 44100)
    int bitsPerSample;       // bits per sample (always 16 for fixed-point output)
    int outputSamps;         // number of output samples (L+R interleaved)
    int layer;               // MPEG layer (1, 2, or 3)
    int version;             // MPEG version (0=MPEG-1, 1=MPEG-2, 2=MPEG-2.5)
} MP3FrameInfo;

// ── Error codes (must match upstream mp3dec.h exactly) ─────────────────────
enum {
    ERR_MP3_NONE                =  0,
    ERR_MP3_INDATA_UNDERFLOW    = -1,
    ERR_MP3_MAINDATA_UNDERFLOW  = -2,
    ERR_MP3_FREE_BITRATE_SYNC   = -3,
    ERR_MP3_OUT_OF_MEMORY       = -4,
    ERR_MP3_NULL_POINTER        = -5,
    ERR_MP3_INVALID_FRAMEHEADER = -6,
    ERR_MP3_INVALID_SIDEINFO    = -7,
    ERR_MP3_INVALID_SCALEFACT   = -8,
    ERR_MP3_INVALID_HUFFCODES   = -9,
    ERR_MP3_INVALID_DEQUANTIZE  = -10,
    ERR_MP3_INVALID_IMDCT       = -11,
    ERR_MP3_INVALID_SUBBAND     = -12,

    ERR_UNKNOWN                 = -9999
};

// ── Decoder API (signatures match upstream mp3dec.h exactly) ───────────────
HMP3Decoder MP3InitDecoder(void);
void MP3FreeDecoder(HMP3Decoder hMP3Decoder);
int MP3Decode(HMP3Decoder hMP3Decoder, unsigned char** inbuf, int* bytesLeft, short* outbuf, int useSize);
void MP3GetLastFrameInfo(HMP3Decoder hMP3Decoder, MP3FrameInfo* mp3FrameInfo);
int MP3FindSyncWord(unsigned char* buf, int nBytes);

#ifdef __cplusplus
}
#endif
