#ifndef PSS_FORMAT_H
#define PSS_FORMAT_H

// ---------------------------------------------------------------------------
// PS2 "PSS" video container (PlayStation Stream)
//
// A PSS file is an MPEG-1/2 Program Stream: a sequence of start-code
// prefixed packs and PES packets. Two things make it not-quite-generic MPEG-PS:
//
//   - the video elementary stream carried in PES packets with stream id
//     0xE0-0xEF is MPEG2 video, decoded/encoded via libavcodec elsewhere.
//   - audio does not use a standard private-stream payload (AC3/DTS/etc).
//     Instead the private_stream_1 (0xBD) payload starts with Sony's own
//     "SShd"/"SSbd" sub-header describing PCM or SPU2-ADPCM (VAG) audio.
//     A file can carry more than one of these back to back - typically one
//     per dub language - each with its own SShd/SSbd pair; the parser tells
//     tracks apart purely from each SSbd's declared size (there's no track
//     count or index anywhere in the format), so a new "SShd" is expected to
//     appear as soon as the previous track's declared data has all arrived.
//
// See: https://rewiki.miraheze.org/wiki/PlayStation_PSS_Video
// ---------------------------------------------------------------------------

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "memory/memory.h"

typedef enum {
    PSS_AUDIO_PCM16_BE  = 0,
    PSS_AUDIO_PCM16_LE  = 1,
    PSS_AUDIO_VAG_ADPCM = 2,
} PssAudioType;

typedef struct {
    bool     present;
    PssAudioType type;
    uint32_t sampleRate;
    uint32_t channels;
    uint32_t interleave;       // bytes; spec requires 512
    int32_t  loopStartBlock;
    int32_t  loopEndBlock;

    // Raw payload exactly as it sits in the container: SPU2-ADPCM bytes for
    // PSS_AUDIO_VAG_ADPCM, or PCM samples (big or little endian per `type`)
    // for the PCM variants. Owned by the arena passed to PssContainer_Parse.
    uint8_t *data;
    uint32_t dataSize;
} PssAudioTrack;

typedef struct {
    bool     present;

    // Concatenation, in file order, of every video PES packet's payload.
    // This is a contiguous MPEG2 elementary stream ready to hand to an
    // MPEG2 decoder (e.g. libavcodec) - PES packet boundaries need not (and
    // generally don't) line up with picture boundaries in MPEG-PS, so the
    // decoder is expected to find its own start codes inside this buffer.
    uint8_t *data;
    uint32_t size;

    // Not read by PssContainer_Parse (frame rate lives in the ES's own
    // MPEG2 sequence header, not this container's framing) - but
    // PssContainer_Write needs it to stamp per-picture PTS values, so a
    // caller building a container to save must fill it in (e.g. from
    // whatever it decoded the source frame rate as).
    double fps;
} PssVideoTrack;

typedef struct {
    PssVideoTrack video;

    // Zero or more audio tracks (dub languages), in file order. Arena-owned,
    // like everything else PssContainer_Parse fills in.
    PssAudioTrack *audioTracks;
    uint32_t       audioTrackCount;

    // The PTS (90kHz ticks) of the very first timestamped packet found
    // during parsing - real retail files don't start their video/audio
    // presentation timeline at 0 despite SCR itself starting at 0; every
    // stream in a file (video and every audio track alike) shares the same
    // non-zero startup value instead (verified: 7571 ticks/84ms in one
    // sample file, 6103/68ms in another, identical across all 6 streams of
    // that second file). PssContainer_Write adds this to every PTS it
    // generates, so a round-tripped file keeps the same startup pre-roll
    // the original had rather than always starting at 0 - the PS2 engine
    // appears to use this to know how much decode pipeline warm-up to
    // expect, and audio position drifted noticeably ahead without it (video
    // tolerates the mismatch better, being coarser-grained: one PTS per
    // picture rather than continuous). hasInitialPts is false when parsing
    // found no timestamp at all (or for a container this app built itself,
    // e.g. from an MP4 import, which has no equivalent to carry forward).
    bool     hasInitialPts;
    uint64_t initialPtsTicks;
} PssContainer;

// Parses `size` bytes at `bytes` into `out`. All buffers `out` ends up
// pointing at are allocated from `arena`, which must outlive `out`.
// Returns false and writes a human readable message into outError on
// malformed input (outError/errorCap may be NULL/0 to ignore it).
bool PssContainer_Parse(const uint8_t *bytes, size_t size, TwinStudio_Arena *arena,
                        PssContainer *out, char *outError, size_t errorCap);

// Serializes `container` into a single buffer allocated from `arena`, and
// hands it back via outData/outSize. The (single) video stream is
// interleaved with the first audio track in fixed-size chunks so the result
// streams reasonably even when read sequentially (as PS2 hardware does from
// disc); any further audio tracks are appended afterwards as their own
// chunked run, since only one track plays at a time so there's nothing to
// gain by interleaving them with each other.
bool PssContainer_Write(const PssContainer *container, TwinStudio_Arena *arena,
                        uint8_t **outData, uint32_t *outSize);

#endif // PSS_FORMAT_H
