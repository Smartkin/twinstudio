#include "pss_format.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include <stb_ds.h>

// ---------------------------------------------------------------------------
// Start codes (ISO/IEC 13818-1 system_stream, plus PSS's private_stream_1)
// ---------------------------------------------------------------------------
#define SC_PACK_HEADER      0xBA
#define SC_SYSTEM_HEADER    0xBB
#define SC_PROGRAM_END      0xB9
#define SC_PRIVATE_STREAM_1 0xBD   // Sony SShd/SSbd audio
#define SC_VIDEO_STREAM_LO  0xE0
#define SC_VIDEO_STREAM_HI  0xEF

// A real retail PSS (verified against a Vivendi-built file) repeats
// pack_header every 16384 bytes for the entire file - no PES packet is ever
// allowed to straddle one of these boundaries, the encoder always truncates
// whichever packet is active and continues that stream's data in a fresh
// packet right after the next pack_header. This is almost certainly there so
// the game's disc-streaming code can pick up a valid stream from any
// 16KB-aligned read; without it, a round-tripped file (parses fine here, but
// has only the one pack_header at offset 0) gets silently skipped in-game.
#define PSS_BLOCK_SIZE 16384u
#define PSS_CHUNK_SIZE 4096u       // interleave granularity within a block

static bool SetErr(char *err, size_t cap, const char *fmt, ...) {
    if (err && cap) {
        va_list ap;
        va_start(ap, fmt);
        vsnprintf(err, cap, fmt, ap);
        va_end(ap);
    }
    return false;
}

static uint16_t ReadU16BE(const uint8_t *p) { return (uint16_t)((p[0] << 8) | p[1]); }
static uint32_t ReadU32LE(const uint8_t *p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}
static void PutU32LE(uint8_t *p, uint32_t v) {
    p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); p[2] = (uint8_t)(v >> 16); p[3] = (uint8_t)(v >> 24);
}

// Standard MPEG-2 PES optional header: 3 fixed bytes ('10'+flags, PTS/DTS+other
// flags, PES_header_data_length) followed by header_data_length bytes that
// playback here mostly doesn't need (it free-runs at a constant frame rate
// rather than reading PTS during decode) - except that the very first PTS in
// the file matters for PssContainer_Write to reproduce the original's
// startup pre-roll on a round trip; see PssContainer's hasInitialPts. Works
// identically for private_stream_1 and the video stream, which both carry
// this header.
static bool ParsePesPacket(const uint8_t *bytes, size_t size, size_t *cursor,
                           size_t *payloadOff, size_t *payloadLen,
                           bool *outHasPts, uint64_t *outPts, char *err, size_t errCap) {
    if (*cursor + 2 > size) return SetErr(err, errCap, "PSS: truncated PES packet length at offset %zu", *cursor);
    uint16_t len = ReadU16BE(bytes + *cursor);
    size_t packetStart = *cursor + 2;
    if (packetStart + len > size) return SetErr(err, errCap, "PSS: PES packet at offset %zu runs past end of file", *cursor);
    if (len < 3) return SetErr(err, errCap, "PSS: PES packet at offset %zu too short for its header", *cursor);

    uint8_t headerDataLen = bytes[packetStart + 2];
    if ((size_t)(3 + headerDataLen) > len)
        return SetErr(err, errCap, "PSS: PES header_data_length exceeds packet length at offset %zu", *cursor);

    *outHasPts = false;
    uint8_t ptsDtsFlags = (bytes[packetStart + 1] >> 6) & 0x3;
    if (ptsDtsFlags != 0 && headerDataLen >= 5) {
        const uint8_t *p = bytes + packetStart + 3;
        *outPts = ((uint64_t)((p[0] >> 1) & 0x7) << 30) | ((uint64_t)p[1] << 22) |
                  ((uint64_t)((p[2] >> 1) & 0x7F) << 15) | ((uint64_t)p[3] << 7) | (uint64_t)(p[4] >> 1);
        *outHasPts = true;
    }

    *payloadOff = packetStart + 3 + headerDataLen;
    *payloadLen = len - 3 - headerDataLen;
    *cursor = packetStart + len;
    return true;
}

// Sony SpuStreamHeader + the start of SpuStreamBody: 40 bytes total
// ("SShd" + size(24) + 6 uint32 fields + "SSbd" + size), all little endian.
#define SPU_HEADER_BYTES 40u

// Every private_stream_1 packet's payload starts with a 4-byte tag: a
// constant 3-byte prefix (FF A0 00 in every sample seen so far) followed by
// a 1-byte track index. With one audio track that index is always 0, which
// is indistinguishable from "no tag" until a real multi-track file shows the
// index actually varying - confirmed against a retail PSS with 5 dub-language
// tracks, round-robin interleaved packet by packet (never several tracks'
// worth of data sharing one packet). A track's first-seen packet carries
// "SShd"/"SSbd" right after the tag; every later packet tagged with that same
// index is raw continuation data for it.
#define TRACK_TAG_BYTES 4u
#define MAX_TRACK_TAG_VALUES 256

bool PssContainer_Parse(const uint8_t *bytes, size_t size, TwinStudio_Arena *arena,
                        PssContainer *out, char *outError, size_t errorCap) {
    memset(out, 0, sizeof(*out));

    uint8_t *videoArr = NULL;    // stb_ds dynamic array; video length isn't known up front
    PssAudioTrack *tracksArr = NULL;    // stb_ds dynamic array, one entry per discovered track
    uint32_t *trackWrittenArr = NULL;   // stb_ds array, parallel to tracksArr
    int trackSlot[MAX_TRACK_TAG_VALUES];   // tag byte -> index into tracksArr, -1 if unseen
    for (int i = 0; i < MAX_TRACK_TAG_VALUES; i++) trackSlot[i] = -1;
    size_t   cursor = 0;

    while (cursor + 4 <= size) {
        if (!(bytes[cursor] == 0x00 && bytes[cursor + 1] == 0x00 && bytes[cursor + 2] == 0x01)) {
            SetErr(outError, errorCap, "PSS: lost sync at offset %zu (not a start code)", cursor);
            goto fail;
        }
        uint8_t code = bytes[cursor + 3];
        cursor += 4;

        if (code == SC_PROGRAM_END) {
            break;
        } else if (code == SC_PACK_HEADER) {
            if (cursor + 10 > size) { SetErr(outError, errorCap, "PSS: truncated pack_header"); goto fail; }
            uint8_t stuffing = bytes[cursor + 9] & 0x07;
            cursor += 10;
            if (cursor + stuffing > size) { SetErr(outError, errorCap, "PSS: truncated pack_header stuffing"); goto fail; }
            cursor += stuffing;
        } else if (code == SC_SYSTEM_HEADER) {
            if (cursor + 2 > size) { SetErr(outError, errorCap, "PSS: truncated system_header"); goto fail; }
            uint16_t hlen = ReadU16BE(bytes + cursor);
            cursor += 2;
            if (cursor + hlen > size) { SetErr(outError, errorCap, "PSS: truncated system_header body"); goto fail; }
            cursor += hlen;
        } else if (code == SC_PRIVATE_STREAM_1) {
            size_t payloadOff, payloadLen;
            bool hasPts; uint64_t pts;
            if (!ParsePesPacket(bytes, size, &cursor, &payloadOff, &payloadLen, &hasPts, &pts, outError, errorCap)) goto fail;
            if (hasPts && !out->hasInitialPts) { out->hasInitialPts = true; out->initialPtsTicks = pts; }
            const uint8_t *payload = bytes + payloadOff;
            size_t remaining = payloadLen;

            if (remaining < TRACK_TAG_BYTES) {
                SetErr(outError, errorCap, "PSS: private_stream_1 packet too short for its track tag");
                goto fail;
            }
            uint8_t trackId = payload[3];   // first 3 bytes are a fixed tag we don't need to interpret
            payload   += TRACK_TAG_BYTES;
            remaining -= TRACK_TAG_BYTES;

            int slot = trackSlot[trackId];
            if (slot < 0) {
                // First packet seen for this track id: it must open with
                // SShd/SSbd right after the tag.
                if (remaining < SPU_HEADER_BYTES) {
                    SetErr(outError, errorCap, "PSS: SShd/SSbd header split across PES packets is not supported");
                    goto fail;
                }
                if (memcmp(payload, "SShd", 4) != 0) {
                    SetErr(outError, errorCap, "PSS: expected \"SShd\" signature in private_stream_1");
                    goto fail;
                }
                if (memcmp(payload + 32, "SSbd", 4) != 0) {
                    SetErr(outError, errorCap, "PSS: expected \"SSbd\" signature after SShd");
                    goto fail;
                }

                PssAudioTrack track; memset(&track, 0, sizeof(track));
                track.present        = true;
                track.type           = (PssAudioType)ReadU32LE(payload + 8);
                track.sampleRate     = ReadU32LE(payload + 12);
                track.channels       = ReadU32LE(payload + 16);
                track.interleave     = ReadU32LE(payload + 20);
                track.loopStartBlock = (int32_t)ReadU32LE(payload + 24);
                track.loopEndBlock   = (int32_t)ReadU32LE(payload + 28);
                track.dataSize       = ReadU32LE(payload + 36);
                track.data           = track.dataSize ? (uint8_t *)TwinStudio_ArenaAlloc(arena, track.dataSize) : NULL;
                arrput(tracksArr, track);
                arrput(trackWrittenArr, 0u);

                slot = (int)(arrlenu(tracksArr) - 1);
                trackSlot[trackId] = slot;
                payload   += SPU_HEADER_BYTES;
                remaining -= SPU_HEADER_BYTES;
            }

            {
                PssAudioTrack *cur = &tracksArr[slot];
                uint32_t written = trackWrittenArr[slot];
                uint32_t need = (cur->dataSize > written) ? (cur->dataSize - written) : 0;
                size_t   take = (remaining < need) ? remaining : need;
                if (take && cur->data) memcpy(cur->data + written, payload, take);
                trackWrittenArr[slot] = written + (uint32_t)take;
            }
        } else if (code >= SC_VIDEO_STREAM_LO && code <= SC_VIDEO_STREAM_HI) {
            size_t payloadOff, payloadLen;
            bool hasPts; uint64_t pts;
            if (!ParsePesPacket(bytes, size, &cursor, &payloadOff, &payloadLen, &hasPts, &pts, outError, errorCap)) goto fail;
            if (hasPts && !out->hasInitialPts) { out->hasInitialPts = true; out->initialPtsTicks = pts; }
            if (payloadLen) {
                size_t oldLen = arrlenu(videoArr);
                arrsetlen(videoArr, oldLen + payloadLen);
                memcpy(videoArr + oldLen, bytes + payloadOff, payloadLen);
            }
        } else {
            // Generic length-prefixed section (padding_stream 0xBE,
            // program_stream_map 0xBC, and anything else we don't care
            // about the contents of) - skip it by its own length field.
            if (cursor + 2 > size) { SetErr(outError, errorCap, "PSS: truncated section at offset %zu", cursor - 4); goto fail; }
            uint16_t len = ReadU16BE(bytes + cursor);
            cursor += 2;
            if (cursor + len > size) { SetErr(outError, errorCap, "PSS: section at offset %zu runs past end of file", cursor - 6); goto fail; }
            cursor += len;
        }
    }

    if (arrlenu(videoArr) > 0) {
        out->video.present = true;
        out->video.size = (uint32_t)arrlenu(videoArr);
        out->video.data = (uint8_t *)TwinStudio_ArenaAlloc(arena, out->video.size);
        memcpy(out->video.data, videoArr, out->video.size);
    }
    arrfree(videoArr);

    // A truncated file can leave any track (not just whichever was last
    // discovered - they're round-robin interleaved, not sequential) short of
    // its declared size; report what actually arrived rather than
    // discarding it.
    if (arrlenu(tracksArr) > 0) {
        for (size_t i = 0; i < arrlenu(tracksArr); i++) {
            if (trackWrittenArr[i] != tracksArr[i].dataSize) {
                fprintf(stderr, "pss: audio track %zu truncated (%u/%u bytes)\n",
                       i, trackWrittenArr[i], tracksArr[i].dataSize);
                tracksArr[i].dataSize = trackWrittenArr[i];
            }
        }

        out->audioTrackCount = (uint32_t)arrlenu(tracksArr);
        out->audioTracks = (PssAudioTrack *)TwinStudio_ArenaAlloc(arena, out->audioTrackCount * sizeof(PssAudioTrack));
        memcpy(out->audioTracks, tracksArr, out->audioTrackCount * sizeof(PssAudioTrack));
    }
    arrfree(tracksArr);
    arrfree(trackWrittenArr);

    if (!out->video.present && out->audioTrackCount == 0)
        return SetErr(outError, errorCap, "PSS: file has no video or audio stream");
    return true;

fail:
    arrfree(videoArr);
    arrfree(tracksArr);
    arrfree(trackWrittenArr);
    return false;
}

// ---------------------------------------------------------------------------
// Writer
// ---------------------------------------------------------------------------

typedef struct { uint8_t *buf; size_t cap; size_t pos; } PssByteWriter;

static void BwU8(PssByteWriter *w, uint8_t v) {
    if (w->pos < w->cap) w->buf[w->pos] = v;
    w->pos++;
}
static void BwU16BE(PssByteWriter *w, uint16_t v) { BwU8(w, (uint8_t)(v >> 8)); BwU8(w, (uint8_t)v); }
static void BwU32BE(PssByteWriter *w, uint32_t v) {
    BwU8(w, (uint8_t)(v >> 24)); BwU8(w, (uint8_t)(v >> 16)); BwU8(w, (uint8_t)(v >> 8)); BwU8(w, (uint8_t)v);
}
static void BwBytes(PssByteWriter *w, const uint8_t *data, size_t n) {
    if (w->pos + n <= w->cap) memcpy(w->buf + w->pos, data, n);
    w->pos += n;
}

// Minimal MSB-first bit writer, used only for the handful of bit-packed
// fields in pack_header/system_header (everything else in this format is
// byte aligned). `out` must already be zeroed for `sizeof` bits worth of space.
typedef struct { uint8_t *out; int bitPos; } PssBitWriter;

static void BitPut(PssBitWriter *bw, uint32_t value, int nbits) {
    for (int i = nbits - 1; i >= 0; i--) {
        int byteIdx = bw->bitPos >> 3;
        int bitIdx  = 7 - (bw->bitPos & 7);
        if ((value >> i) & 1) bw->out[byteIdx] |= (uint8_t)(1u << bitIdx);
        bw->bitPos++;
    }
}

// 5-byte PTS-only timestamp field (ISO/IEC 13818-1 2.4.3.7). `marker4` is the
// 4-bit pattern in the top nibble of the first byte - 0x2 for "PTS only".
static void PutPts(uint8_t *buf, uint8_t marker4, uint64_t pts) {
    buf[0] = (uint8_t)((marker4 << 4) | (((pts >> 30) & 0x7) << 1) | 1);
    buf[1] = (uint8_t)((pts >> 22) & 0xFF);
    buf[2] = (uint8_t)((((pts >> 15) & 0x7F) << 1) | 1);
    buf[3] = (uint8_t)((pts >> 7) & 0xFF);
    buf[4] = (uint8_t)(((pts & 0x7F) << 1) | 1);
}

// `prefix`/`prefixLen` is the raw bytes to place between the PES optional
// header and `payload` (e.g. the private_stream_1 track tag); pass NULL/0
// for none.
static void WritePesPacket(PssByteWriter *w, uint8_t streamId, const uint8_t *prefix, size_t prefixLen,
                           const uint8_t *payload, size_t len, bool withPts, uint64_t pts) {
    // Real retail files reserve a fixed 10-byte optional PES header on
    // *every* packet, video and audio, whether or not it actually carries a
    // PTS - verified byte-for-byte against both sample files: a packet with
    // no timestamp (PTS_DTS_flags='00') still declares header_data_length
    // 10 and fills all of it with stuffing_byte (0xFF); one with only a PTS
    // fills 5 real bytes then pads the other 5 the same way. The previous
    // version of this writer used the spec-minimum 0 or 5 bytes instead,
    // which is legal MPEG-PS but not what these files (or, evidently, the
    // game's own parser) actually do.
    uint8_t  headerDataLen = 10;
    uint16_t pesLen = (uint16_t)(3 + headerDataLen + prefixLen + len);   // caller keeps this << 65535

    BwU32BE(w, 0x00000100u | streamId);
    BwU16BE(w, pesLen);
    BwU8(w, 0x80);                          // '10' + scrambling/priority/alignment/copyright/original = 0
    BwU8(w, withPts ? 0x80 : 0x00);          // PTS_DTS_flags = '10' (PTS only) or none
    BwU8(w, headerDataLen);
    if (withPts) {
        uint8_t ptsBuf[5];
        PutPts(ptsBuf, 0x2, pts);
        BwBytes(w, ptsBuf, 5);
        for (int i = 0; i < 5; i++) BwU8(w, 0xFF);   // stuffing_byte, pad to the fixed 10
    } else {
        for (int i = 0; i < 10; i++) BwU8(w, 0xFF);  // stuffing_byte, no timestamp at all
    }
    if (prefixLen) BwBytes(w, prefix, prefixLen);
    BwBytes(w, payload, len);
}

// Fallback program_mux_rate/rate_bound (units of 50 bytes/sec) used only
// when no duration is known at all (no video pictures and no audio, or a
// zero frame rate) - otherwise PssContainer_Write computes the real average
// from the container's actual size and duration, matching how a real
// encoder ties its declared rate to what it's actually producing.
#define PSS_MUX_RATE_FALLBACK 5000u

static void WritePackHeader(PssByteWriter *w, uint64_t scr, uint32_t muxRateField) {
    BwU32BE(w, 0x000001BA);
    uint8_t fixed[10]; memset(fixed, 0, sizeof(fixed));
    PssBitWriter bw = { fixed, 0 };
    BitPut(&bw, 0x1, 2);                          // '01'
    BitPut(&bw, (uint32_t)((scr >> 30) & 0x7), 3); // SCR[32..30]
    BitPut(&bw, 1, 1);
    BitPut(&bw, (uint32_t)((scr >> 15) & 0x7FFF), 15); // SCR[29..15]
    BitPut(&bw, 1, 1);
    BitPut(&bw, (uint32_t)(scr & 0x7FFF), 15);     // SCR[14..0]
    BitPut(&bw, 1, 1);
    BitPut(&bw, 0, 9);                             // SCR extension
    BitPut(&bw, 1, 1);
    BitPut(&bw, muxRateField, 22);                  // program_mux_rate
    BitPut(&bw, 1, 1);
    BitPut(&bw, 1, 1);
    BitPut(&bw, 0x1F, 5);   // reserved
    BitPut(&bw, 0, 3);      // pack_stuffing_length
    BwBytes(w, fixed, sizeof(fixed));
}

// Fills exactly `totalBytes` (including its own 6-byte header) with a
// padding_stream packet - the only valid way to consume a small gap that no
// pending stream has enough data or room to fill itself, keeping every later
// pack_header exactly PSS_BLOCK_SIZE-aligned like a real file's.
static void WritePaddingStream(PssByteWriter *w, size_t totalBytes) {
    if (totalBytes < 6) return;   // caller is expected to avoid this; see the call site
    uint16_t payloadLen = (uint16_t)(totalBytes - 6);
    BwU32BE(w, 0x000001BE);
    BwU16BE(w, payloadLen);
    for (uint16_t i = 0; i < payloadLen; i++) BwU8(w, 0xFF);
}

bool PssContainer_Write(const PssContainer *container, TwinStudio_Arena *arena,
                        uint8_t **outData, uint32_t *outSize) {
    if (!container->video.present && container->audioTrackCount == 0) return false;
    if (container->video.present && (!container->video.data || container->video.size == 0)) return false;

    uint32_t videoSize = container->video.present ? container->video.size : 0;
    uint32_t numTracks = container->audioTrackCount;

    // One SShd/SSbd-prefixed logical buffer per audio track, so the
    // interleave loop below can chunk each one the same way it chunks video.
    uint8_t **audioLogical     = numTracks ? (uint8_t **)TWIN_MALLOC(numTracks * sizeof(uint8_t *)) : NULL;
    size_t   *audioLogicalSize = numTracks ? (size_t *)TWIN_MALLOC(numTracks * sizeof(size_t)) : NULL;
    size_t    audioLogicalTotal = 0;
    for (uint32_t t = 0; t < numTracks; t++) {
        const PssAudioTrack *track = &container->audioTracks[t];
        size_t sz = SPU_HEADER_BYTES + track->dataSize;
        uint8_t *buf2 = (uint8_t *)TWIN_MALLOC(sz);
        uint8_t *p = buf2;
        memcpy(p, "SShd", 4); p += 4;
        PutU32LE(p, 24); p += 4;
        PutU32LE(p, (uint32_t)track->type); p += 4;
        PutU32LE(p, track->sampleRate); p += 4;
        PutU32LE(p, track->channels); p += 4;
        PutU32LE(p, track->interleave); p += 4;
        PutU32LE(p, (uint32_t)track->loopStartBlock); p += 4;
        PutU32LE(p, (uint32_t)track->loopEndBlock); p += 4;
        memcpy(p, "SSbd", 4); p += 4;
        PutU32LE(p, track->dataSize); p += 4;
        if (track->dataSize) memcpy(p, track->data, track->dataSize);
        audioLogical[t] = buf2;
        audioLogicalSize[t] = sz;
        audioLogicalTotal += sz;
    }

    uint32_t numVideoChunks = videoSize ? (videoSize + PSS_CHUNK_SIZE - 1) / PSS_CHUNK_SIZE : 0;
    uint32_t numAudioChunks = 0;
    for (uint32_t t = 0; t < numTracks; t++)
        numAudioChunks += audioLogicalSize[t] ? (uint32_t)((audioLogicalSize[t] + PSS_CHUNK_SIZE - 1) / PSS_CHUNK_SIZE) : 0;

    // Generous upper bound: raw payload plus per-packet framing (19 bytes
    // per video packet: 4 start code + 2 length + 3 fixed PES flags + the
    // fixed 10-byte optional header every packet now carries regardless of
    // whether it has a real PTS; audio packets carry an extra 4-byte track
    // tag on top of that), the fixed pack/system headers and the end code,
    // plus a pack_header (and, worst case, a padding_stream) every
    // PSS_BLOCK_SIZE bytes of payload.
    size_t totalPayload = (size_t)videoSize + audioLogicalTotal;
    size_t numBlocks = totalPayload / PSS_BLOCK_SIZE + 16;   // +16 covers rounding/overhead margin

    size_t cap = 14 /* initial pack_header */ + 4 + 2 + 6 + 2 * 3 /* system_header, 2 streams */ + 4 /* end code */
               + videoSize + (size_t)numVideoChunks * 19
               + audioLogicalTotal + (size_t)numAudioChunks * 23
               + numBlocks * (14 + 32)
               + 64;

    // --- picture boundaries + per-track/overall duration, for PTS/SCR ---
    //
    // A real retail file stamps a real, increasing PTS on every single
    // audio packet, and on whichever video packet contains the start of a
    // new picture (verified against both sample files: every one of ~320
    // audio packets carried a PTS, video only on ~1 in 11). This writer
    // used to hardcode PTS=0 on just the first packet of each stream -
    // harmless to this codebase's own decoder (which free-runs at a
    // constant frame rate and never reads PTS) but not to the PS2 game
    // engine's real AV-sync logic, which does - that's what was producing
    // audio that stutters or starts from the wrong position in-game.
    // MPEG2 forbids the start-code prefix 0x000001 from occurring anywhere
    // in a properly encoded stream except at real start codes (bitstuffing
    // guarantees it), so scanning for 00 00 01 00 (picture_start_code) is a
    // safe, standard way to find every picture's byte offset up front.
    size_t *pictureStarts = NULL;   // stb_ds array of byte offsets into video.data
    for (uint32_t i = 0; videoSize >= 4 && i + 4 <= videoSize; i++) {
        const uint8_t *d = container->video.data;
        if (d[i] == 0 && d[i + 1] == 0 && d[i + 2] == 1 && d[i + 3] == 0x00)
            arrput(pictureStarts, (size_t)i);
    }
    double fps = container->video.fps > 0.0 ? container->video.fps : 25.0;
    size_t numPictures = arrlenu(pictureStarts);
    uint64_t videoDurationTicks = numPictures ? (uint64_t)((double)numPictures * 90000.0 / fps + 0.5) : 0;

    // Real files don't start their presentation timeline at 0 despite SCR
    // itself starting at 0 - see PssContainer::hasInitialPts. Carrying the
    // original file's own pre-roll forward (rather than defaulting to 0)
    // is what makes a round-tripped file's audio start from the same
    // position as the original instead of noticeably ahead of it.
    uint64_t ptsOffset = container->hasInitialPts ? container->initialPtsTicks : 0;

    // Byte offset within a track's data is exactly proportional to sample
    // position for every format this writer produces (PCM16 is trivially
    // linear; SPU2-ADPCM compresses at a fixed 16-bytes-per-28-samples-per-
    // channel ratio with no variable-rate blocks), so per-packet PTS can be
    // interpolated from byte offset without decoding anything.
    uint64_t *audioDurationTicks = numTracks ? (uint64_t *)TWIN_MALLOC(numTracks * sizeof(uint64_t)) : NULL;
    uint64_t totalDurationTicks = videoDurationTicks;
    for (uint32_t t = 0; t < numTracks; t++) {
        const PssAudioTrack *track = &container->audioTracks[t];
        uint64_t frames = 0;
        if (track->channels && track->dataSize) {
            frames = (track->type == PSS_AUDIO_VAG_ADPCM)
                ? (uint64_t)(track->dataSize / ((uint64_t)track->channels * 16u)) * 28ull
                : (uint64_t)track->dataSize / ((uint64_t)track->channels * 2u);
        }
        audioDurationTicks[t] = track->sampleRate ? frames * 90000ull / track->sampleRate : 0;
        if (audioDurationTicks[t] > totalDurationTicks) totalDurationTicks = audioDurationTicks[t];
    }

    // program_mux_rate/rate_bound (units of 50 bytes/sec): the real average
    // byte rate this container is actually produced at, so SCR (derived
    // from it below the same way a real encoder ties the two together)
    // tracks actual elapsed playback time instead of an assumed constant -
    // a custom import's real bitrate can be wildly different from any
    // single fixed guess, and that mismatch compounding over the file was
    // producing the audio stutter seen on longer/higher-bitrate imports.
    uint32_t muxRateField = PSS_MUX_RATE_FALLBACK;
    if (totalDurationTicks > 0) {
        double seconds = (double)totalDurationTicks / 90000.0;
        double field = ((double)totalPayload / seconds) / 50.0;
        if (field < 1.0) field = 1.0;
        if (field > (double)0x3FFFFEu) field = (double)0x3FFFFEu;
        muxRateField = (uint32_t)(field + 0.5);
    }

    uint8_t *buf = (uint8_t *)TWIN_MALLOC(cap);
    if (!buf) {
        for (uint32_t t = 0; t < numTracks; t++) TWIN_FREE(audioLogical[t]);
        if (audioLogical) TWIN_FREE(audioLogical);
        if (audioLogicalSize) TWIN_FREE(audioLogicalSize);
        if (audioDurationTicks) TWIN_FREE(audioDurationTicks);
        arrfree(pictureStarts);
        return false;
    }
    PssByteWriter w = { buf, cap, 0 };

    // --- pack_header ---
    WritePackHeader(&w, 0, muxRateField);

    // --- system_header ---
    {
        // One entry per stream id, not per audio track: every track shares
        // stream id 0xBD (private_stream_1), same as multiple video PES
        // packets all sharing 0xE0.
        int numStreams = (container->video.present ? 1 : 0) + (numTracks > 0 ? 1 : 0);
        BwU32BE(&w, 0x000001BB);
        BwU16BE(&w, (uint16_t)(6 + 3 * numStreams));

        uint8_t fixed[6]; memset(fixed, 0, sizeof(fixed));
        PssBitWriter sw = { fixed, 0 };
        BitPut(&sw, 1, 1);
        BitPut(&sw, muxRateField, 22);         // rate_bound
        BitPut(&sw, 1, 1);
        BitPut(&sw, 1, 6);      // audio_bound
        BitPut(&sw, 0, 1);      // fixed_flag
        BitPut(&sw, 0, 1);      // CSPS_flag
        BitPut(&sw, 0, 1);      // system_audio_lock_flag
        BitPut(&sw, 0, 1);      // system_video_lock_flag
        BitPut(&sw, 1, 1);
        BitPut(&sw, 1, 5);      // video_bound
        BitPut(&sw, 0, 1);      // packet_rate_restriction_flag
        BitPut(&sw, 0x7F, 7);   // reserved_bits
        BwBytes(&w, fixed, sizeof(fixed));

        if (container->video.present) {
            uint8_t sb[3]; memset(sb, 0, sizeof(sb));
            PssBitWriter tw = { sb, 0 };
            BitPut(&tw, 0xE0, 8);
            BitPut(&tw, 0x3, 2);
            BitPut(&tw, 1, 1);     // P-STD_buffer_bound_scale (1 => 1024-byte units)
            BitPut(&tw, 2, 13);    // ~2KB buffer bound, placeholder
            BwBytes(&w, sb, sizeof(sb));
        }
        if (numTracks > 0) {
            uint8_t sb[3]; memset(sb, 0, sizeof(sb));
            PssBitWriter tw = { sb, 0 };
            BitPut(&tw, 0xBD, 8);
            BitPut(&tw, 0x3, 2);
            BitPut(&tw, 0, 1);     // scale (0 => 128-byte units)
            BitPut(&tw, 32, 13);   // ~4KB buffer bound, placeholder
            BwBytes(&w, sb, sizeof(sb));
        }
    }

    // --- interleaved video + all audio tracks (round-robin), block-aligned ---
    //
    // Matches what a real retail PSS with several dub-language tracks does:
    // one video chunk, then one chunk from every audio track in turn, so any
    // track can be selected without needing to seek elsewhere in the file.
    // Each audio packet is tagged FF A0 00 <track index>, which is how a
    // reader (including PssContainer_Parse) tells which track a packet's
    // data belongs to.
    //
    // On top of that, no packet written here is ever allowed to cross a
    // PSS_BLOCK_SIZE boundary - whichever packet would, is instead sized to
    // land exactly on it, and a fresh pack_header goes right after (see the
    // PSS_BLOCK_SIZE comment above). A round that finds no stream with both
    // data and room left falls back to a padding_stream packet to consume
    // the last few bytes, so the boundary still lands exactly on schedule.
    {
        // Streams are scheduled by weighted deficit round-robin: index 0 is
        // video (if present), indices after that are audio tracks in order.
        // Each stream's "ideal" bytes delivered so far is its own size's
        // share of everything written so far; whichever stream is furthest
        // behind that ideal (byte deficit, not fixed alternation) writes
        // next. This matters a great deal: a real retail file's audio and
        // video byte volumes are almost never close to 1:1 (single-track
        // vivendi.pss is ~85% video / 15% audio by bitrate; 5-track
        // B01_A.pss is ~44% video / 56% audio combined across its dub
        // tracks) - naively alternating one video chunk with one chunk per
        // audio track, as this writer used to, delivers audio far faster
        // than its real playback rate (up to 6x too fast for vivendi.pss),
        // which is what was making audio start noticeably ahead of the
        // track in-game even after PTS/SCR values themselves were correct.
        bool   videoStreamPresent = videoSize > 0;
        uint32_t numStreams = (videoStreamPresent ? 1u : 0u) + numTracks;
        size_t *streamSize    = numStreams ? (size_t *)TWIN_MALLOC(numStreams * sizeof(size_t)) : NULL;
        size_t *streamWritten = numStreams ? (size_t *)TWIN_MALLOC(numStreams * sizeof(size_t)) : NULL;
        if (videoStreamPresent) { streamSize[0] = videoSize; streamWritten[0] = 0; }
        for (uint32_t t = 0; t < numTracks; t++) {
            uint32_t s = (videoStreamPresent ? 1u : 0u) + t;
            streamSize[s] = audioLogicalSize[t];
            streamWritten[s] = 0;
        }
        size_t totalWrittenAllStreams = 0;
        size_t nextPicIdx = 0;   // index into pictureStarts of the next not-yet-consumed picture

        // A track's very first packet (the one carrying its SShd/SSbd
        // header) must appear in the file before the next track's first
        // packet - PssContainer_Parse assigns audioTracks[] slots by each
        // tag's first-seen order, not by the tag value itself, so writing
        // them out of order silently swaps tracks on read back. Real files
        // do this too: every dub track's SShd header clusters near the
        // start, in index order, before the bulk interleaving proper
        // begins. audioTrack `t` is eligible to start once its index
        // equals this counter; every already-started track keeps competing
        // on ordinary deficit terms regardless of order.
        uint32_t nextAudioTrackToStart = 0;

        size_t nextBoundary = PSS_BLOCK_SIZE;   // the initial pack_header+system_header already covers [0, this)

        for (;;) {
            if (w.pos >= nextBoundary) {
                uint64_t scr = totalPayload
                    ? (uint64_t)((double)w.pos * (double)totalDurationTicks / (double)totalPayload + 0.5)
                    : 0;
                WritePackHeader(&w, scr, muxRateField);
                nextBoundary += PSS_BLOCK_SIZE;
            }

            // Pick whichever not-yet-exhausted stream is furthest behind its
            // ideal byte share AND fits (with its fixed per-packet overhead)
            // in whatever room is left before the next block boundary.
            int    bestIdx = -1;
            double bestDeficit = -1.0;
            bool   anyLeft = false;
            for (uint32_t s = 0; s < numStreams; s++) {
                if (streamWritten[s] >= streamSize[s]) continue;
                anyLeft = true;
                bool isVideo = videoStreamPresent && s == 0;
                if (!isVideo) {
                    uint32_t t = s - (videoStreamPresent ? 1u : 0u);
                    if (streamWritten[s] == 0 && t != nextAudioTrackToStart) continue;   // not this track's turn to start yet
                }
                size_t overhead = isVideo ? (9 + 10) : (9 + 4 + 10);
                size_t roomLeft = nextBoundary - w.pos;
                if (roomLeft <= overhead) continue;
                double ideal = totalPayload
                    ? (double)totalWrittenAllStreams * (double)streamSize[s] / (double)totalPayload
                    : 0.0;
                double deficit = ideal - (double)streamWritten[s];
                if (bestIdx < 0 || deficit > bestDeficit) { bestIdx = (int)s; bestDeficit = deficit; }
            }

            if (!anyLeft) break;   // every stream fully written

            if (bestIdx < 0) {
                // Something's left but none of it fits in what remains of
                // this block - pad it out so the next pack_header still
                // lands exactly on the boundary (see PSS_BLOCK_SIZE above).
                size_t gap = nextBoundary - w.pos;
                if (gap >= 6) WritePaddingStream(&w, gap);
                else nextBoundary = w.pos;
                continue;
            }

            size_t roomLeft = nextBoundary - w.pos;
            size_t take;
            if (videoStreamPresent && bestIdx == 0) {
                size_t overhead = 9 + 10;
                size_t videoOff = streamWritten[0];
                size_t want = (videoSize - videoOff) < PSS_CHUNK_SIZE ? (videoSize - videoOff) : PSS_CHUNK_SIZE;
                take = want < (roomLeft - overhead) ? want : (roomLeft - overhead);

                bool withPts = false;
                uint64_t vpts = 0;
                while (nextPicIdx < numPictures && pictureStarts[nextPicIdx] < videoOff + take) {
                    if (!withPts && pictureStarts[nextPicIdx] >= videoOff) {
                        withPts = true;
                        vpts = ptsOffset + (uint64_t)((double)nextPicIdx * 90000.0 / fps + 0.5);
                    }
                    nextPicIdx++;
                }

                WritePesPacket(&w, 0xE0, NULL, 0, container->video.data + videoOff, take, withPts, vpts);
            } else {
                uint32_t t = (uint32_t)bestIdx - (videoStreamPresent ? 1u : 0u);
                size_t overhead = 9 + 4 + 10;
                size_t audioOff = streamWritten[bestIdx];
                size_t remain = audioLogicalSize[t] - audioOff;
                size_t want = remain < PSS_CHUNK_SIZE ? remain : PSS_CHUNK_SIZE;
                take = want < (roomLeft - overhead) ? want : (roomLeft - overhead);

                // Byte offset into the track's own data (excluding the
                // SShd/SSbd header prefixed onto audioLogical) is exactly
                // proportional to sample position - see the comment above
                // audioDurationTicks's setup.
                size_t dataOff = (audioOff >= SPU_HEADER_BYTES) ? (audioOff - SPU_HEADER_BYTES) : 0;
                const PssAudioTrack *track = &container->audioTracks[t];
                uint64_t apts = ptsOffset + (track->dataSize
                    ? (uint64_t)((double)dataOff * (double)audioDurationTicks[t] / (double)track->dataSize + 0.5)
                    : 0);

                uint8_t tag[4] = { 0xFF, 0xA0, 0x00, (uint8_t)t };
                WritePesPacket(&w, 0xBD, tag, sizeof(tag), audioLogical[t] + audioOff, take, true, apts);

                if (audioOff == 0 && t == nextAudioTrackToStart) nextAudioTrackToStart++;
            }

            streamWritten[bestIdx] += take;
            totalWrittenAllStreams += take;
        }

        if (streamSize) TWIN_FREE(streamSize);
        if (streamWritten) TWIN_FREE(streamWritten);
    }

    BwU32BE(&w, 0x000001B9);   // MPEG_program_end_code

    for (uint32_t t = 0; t < numTracks; t++) TWIN_FREE(audioLogical[t]);
    if (audioLogical) TWIN_FREE(audioLogical);
    if (audioLogicalSize) TWIN_FREE(audioLogicalSize);
    if (audioDurationTicks) TWIN_FREE(audioDurationTicks);
    arrfree(pictureStarts);

    if (w.pos > cap) {
        fprintf(stderr, "pss: internal error, write buffer undersized (%zu > %zu)\n", w.pos, cap);
        TWIN_FREE(buf);
        return false;
    }

    uint8_t *final = (uint8_t *)TwinStudio_ArenaAlloc(arena, w.pos);
    memcpy(final, buf, w.pos);
    TWIN_FREE(buf);

    *outData = final;
    *outSize = (uint32_t)w.pos;
    return true;
}
