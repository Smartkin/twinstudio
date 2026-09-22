#ifndef PSS_CODEC_H
#define PSS_CODEC_H

// ---------------------------------------------------------------------------
// Bridges the raw PSS container (pss_format.h) to actual samples/pixels:
//   - MPEG2 video is decoded/encoded through libavcodec.
//   - SPU2-ADPCM (VAG) / PCM audio is decoded/encoded through the existing
//     PS2 ADPCM codec in common/audio/adpcm.c, which already implements the
//     same format used elsewhere in this codebase for archive music.
// ---------------------------------------------------------------------------

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "memory/memory.h"
#include "pss_format.h"

typedef struct PssVideoDecoder PssVideoDecoder;

// `esData`/`esSize` must outlive the decoder (they are not copied).
PssVideoDecoder *PssVideoDecoder_Create(const uint8_t *esData, uint32_t esSize);
void PssVideoDecoder_Destroy(PssVideoDecoder *dec);

// Decodes and returns the next picture as tightly packed RGBA8. The returned
// pointer is owned by the decoder, sized outW*outH*4, and is only valid
// until the next call (or destroy). Returns false once the stream is
// exhausted.
bool PssVideoDecoder_NextFrame(PssVideoDecoder *dec, const uint8_t **outRgba, int *outW, int *outH);

// Restarts decoding from the beginning of the elementary stream.
void PssVideoDecoder_Rewind(PssVideoDecoder *dec);

// Valid once at least one frame has been decoded; 25fps until then.
double PssVideoDecoder_FrameRate(const PssVideoDecoder *dec);

// ---------------------------------------------------------------------------
// Streaming MPEG2 encoder (RGBA8 in, elementary stream out). Pushed one frame
// at a time rather than pulled, so a caller demuxing an input file can feed
// video frames to this in the same pass it decodes that file's audio track.
// ---------------------------------------------------------------------------
typedef struct PssMpeg2Encoder PssMpeg2Encoder;

// `sarNum`/`sarDen` is the coded picture's pixel (sample) aspect ratio - not
// the display aspect ratio. Pass 1/1 for square pixels; a caller that scaled
// the source to a coded size which isn't itself exactly 4:3 or 16:9 (e.g.
// after rounding down to a multiple of 16 for the PS2's decoder) should
// instead pass whatever SAR makes coded_size * SAR equal the intended
// display aspect ratio exactly - see Mp4ComputeSampleAspectRatio. Every
// retail PSS sample seen has an MPEG2 aspect_ratio_information of exactly
// 4:3 or 16:9; leaving SAR at the implicit 1:1 (square pixel) default for a
// coded size that's merely close to but not exactly one of those makes
// libavcodec fall back to "unspecified", which the game doesn't display
// with a matching scale to what the coded picture actually is.
//
// `bitRate` is the target constant bit rate in bits/sec; 0 defaults to
// 9,000,000, matching every retail PSS sample seen.
PssMpeg2Encoder *PssMpeg2Encoder_Create(int width, int height, double fps, int sarNum, int sarDen,
                                        int64_t bitRate, char *outError, size_t errorCap);

// `rgba` must be width*height*4 bytes (as passed to Create).
bool PssMpeg2Encoder_PushRgba(PssMpeg2Encoder *enc, const uint8_t *rgba);

// Flushes remaining frames, copies the accumulated elementary stream into
// `arena`, and destroys the encoder either way. The stream itself is
// spooled to a temp file as it's encoded rather than held in memory (see
// PssMpeg2Encoder's definition) - this is the one point it's read back in,
// directly into `arena`, so peak memory stays close to the final size
// instead of several times over it.
bool PssMpeg2Encoder_Finish(PssMpeg2Encoder *enc, TwinStudio_Arena *arena,
                            uint8_t **outEs, uint32_t *outEsSize);

// Destroys the encoder without finishing it, e.g. on an error path.
void PssMpeg2Encoder_Abort(PssMpeg2Encoder *enc);

// Decodes a PssAudioTrack (PCM16 BE/LE or SPU2-ADPCM) into interleaved
// signed 16-bit PCM, allocated from `arena`.
bool PssAudioTrack_DecodeToPcm(const PssAudioTrack *track, TwinStudio_Arena *arena,
                               int16_t **outPcm, uint32_t *outFrameCount);

// Encodes interleaved signed 16-bit PCM (mono or stereo) as `type`, filling
// in a fresh PssAudioTrack allocated from `arena`. No loop point is set
// (loopStartBlock/loopEndBlock = -1) since arbitrary imported/decoded audio
// has none. PCM16 output is block-interleaved the same way SPU2-ADPCM is
// (see PssAudioTrack_DecodeToPcm's comment) - the inverse of that function,
// so re-encoding whatever a track was opened as round-trips through here
// without changing its declared type. Every retail PSS sample seen so far
// uses PCM16LE, never VAG-ADPCM, so callers with no better information
// should default to PSS_AUDIO_PCM16_LE rather than VAG-ADPCM.
bool PssAudioTrack_EncodeFromPcm(TwinStudio_Arena *arena, const int16_t *pcm, uint32_t frameCount,
                                 uint32_t sampleRate, uint32_t channels, PssAudioType type,
                                 PssAudioTrack *outTrack);

#endif // PSS_CODEC_H
