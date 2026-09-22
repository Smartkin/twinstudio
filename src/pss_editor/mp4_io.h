#ifndef PSS_MP4_IO_H
#define PSS_MP4_IO_H

// ---------------------------------------------------------------------------
// mp4 import/export via ffmpeg's libavformat, layered on top of the codec
// helpers in pss_codec.h. Both functions are synchronous and meant to be
// called from a background worker thread (they can take a while on a big
// file), matching the rest of this codebase's loader pattern.
// ---------------------------------------------------------------------------

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "memory/memory.h"
#include "pss_codec.h"

typedef struct {
    uint8_t *videoEs;
    uint32_t videoEsSize;
    int      width, height;
    double   fps;

    int16_t *pcm;          // NULL if the source has no audio track
    uint32_t pcmFrames;
    uint32_t sampleRate;
    uint32_t channels;
} Mp4ImportResult;

typedef void (*PssProgressFn)(void *user, float fraction /* 0..1, -1 for indeterminate */);

// Which standard ratio Mp4ComputeImportTarget's canvas quantization targets
// (see its comment) - PSS_ASPECT_AUTO picks whichever the source is closer
// to, same as always; the other two let a caller override that choice (e.g.
// so the person importing can pick for themselves instead of having it
// decided for them). Meaningless when `Mp4ImportLimits::exact` is set,
// since that canvas size is already fixed.
typedef enum { PSS_ASPECT_AUTO = 0, PSS_ASPECT_4_3, PSS_ASPECT_16_9 } PssTargetAspect;

// How Mp4Import fills the canvas when the source's own aspect ratio doesn't
// already match it exactly.
typedef enum {
    // Non-uniform scale: the source fills the canvas completely, distorted
    // if its own aspect ratio differs. Always fills the screen; the picture
    // itself may look stretched/squashed if the source aspect is far off.
    PSS_FIT_STRETCH = 0,
    // Uniform scale-to-fit, source aspect ratio preserved, centered with
    // black letterbox/pillarbox bars filling whatever's left over. Never
    // distorts the picture; leaves black bars instead.
    PSS_FIT_LETTERBOX,
} PssFitMode;

// Caps applied by Mp4Import: the game engine this tool targets skips PSS
// videos outright above certain resolution/frame rate combinations (see
// Mp4Import's comment). 0 means "no cap" on that field.
typedef struct {
    int    maxWidth, maxHeight;
    double maxFps;

    // If true, maxWidth/maxHeight is the exact size Mp4Import encodes at -
    // the source is fit to it (see `fit`) rather than the encoded picture
    // itself shrinking in whichever axis the source's aspect ratio doesn't
    // fill. Use when importing to replace a specific existing PSS video:
    // the game does not dynamically resize whatever on-screen area it
    // reserves for a given cutscene to match an oddly-sized replacement,
    // leaving the size difference as untouched black space instead.
    bool   exact;

    PssTargetAspect aspect;   // PSS_ASPECT_AUTO unless the caller wants to override it
    PssFitMode      fit;      // PSS_FIT_STRETCH unless the caller wants letterboxing instead
} Mp4ImportLimits;

// Opens `path` just far enough to read its video stream's dimensions and
// frame rate (no decoding), so a caller can decide whether Mp4Import will
// need to scale it down before committing to a full import.
bool Mp4ProbeVideo(const char *path, int *outWidth, int *outHeight, double *outFps,
                   char *outError, size_t errorCap);

// The exact canvas width/height/fps Mp4Import would encode at for a source
// of `srcWidth`x`srcHeight` at `srcFps`, given `limits` (NULL/all-zero = no
// change) - exposed so a caller can preview what an import will do (e.g.
// for a "this will be scaled and stretched to fill" prompt) without
// actually running it. This is the canvas, not necessarily the source's own aspect
// ratio: with `limits->exact` it's exactly maxWidth x maxHeight; otherwise
// it's the largest multiple-of-16 EXACT 4:3 or 16:9 resolution (whichever
// the source is closer to) that fits within maxWidth x maxHeight - not
// just the source scaled down and floored to a multiple of 16, which
// generally lands close to but not exactly on a standard ratio. Every real
// retail PSS resolution seen is an exact 4:3 or 16:9 ratio; empirically,
// a coded size that's merely close (needing a non-square sample_aspect_
// ratio to declare the intended display ratio) does not fill the screen
// in-game even though the resulting MPEG2 is fully conformant.
void Mp4ComputeImportTarget(int srcWidth, int srcHeight, double srcFps, const Mp4ImportLimits *limits,
                            int *outWidth, int *outHeight, double *outFps);

// The pixel (sample) aspect ratio to encode `outWidth`x`outHeight` at so the
// resulting *display* aspect ratio matches whichever of 4:3 or 16:9 the
// original `srcWidth`x`srcHeight` is closer to - needed because rounding
// `outWidth`/`outHeight` down to a multiple of 16 (see Mp4ComputeImportTarget)
// can leave them not exactly 4:3 or 16:9 themselves, and every retail PSS
// sample's video declares one of those two exactly.
void Mp4ComputeSampleAspectRatio(int srcWidth, int srcHeight, int outWidth, int outHeight,
                                 int *outSarNum, int *outSarDen);

// Opens `path` with ffmpeg's own demuxer (so anything it can play works, not
// just mp4), decodes its video track and re-encodes it to MPEG2, and decodes
// its audio track (if any) to interleaved S16 PCM. Everything in `out` is
// allocated from `arena`.
//
// If `limits` is non-NULL, the video is scaled to whatever canvas size
// `limits` selects (see Mp4ComputeImportTarget), either stretched to fill it
// exactly or letterboxed within it depending on `limits->fit`, and frames
// are dropped to bring the frame rate down to maxFps if it's higher - both
// to fit within the target platform's decoder limits. `out->width/height/
// fps` reflect whatever was actually encoded, which may differ from the
// source.
bool Mp4Import(const char *path, TwinStudio_Arena *arena, const Mp4ImportLimits *limits, Mp4ImportResult *out,
              PssProgressFn progress, void *progressUser, char *outError, size_t errorCap);

// Writes an mp4 file at `path`: video is decoded from `videoDec` (which is
// rewound first) and re-encoded to H.264; `pcm` (optional; pass frameCount 0
// to skip) is re-encoded to AAC. `frameCount` bounds how many video frames to
// export (matching the source's total frame count).
bool Mp4Export(const char *path, PssVideoDecoder *videoDec, uint32_t frameCount,
              const int16_t *pcm, uint32_t pcmFrames, uint32_t sampleRate, uint32_t channels,
              PssProgressFn progress, void *progressUser, char *outError, size_t errorCap);

#endif // PSS_MP4_IO_H
