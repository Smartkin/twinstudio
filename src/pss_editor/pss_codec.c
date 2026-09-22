#include "pss_codec.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <stb_ds.h>

#include "audio/adpcm.h"

#include <libavcodec/avcodec.h>
#include <libavutil/imgutils.h>
#include <libavutil/opt.h>
#include <libavutil/pixdesc.h>
#include <libswscale/swscale.h>

// Decoders built with hwaccels (e.g. d3d11va/d3d12va on Windows) advertise
// their hardware pixel formats first. Without this, the default get_format
// picks the hwaccel format, and since we never set up a hw_frames_ctx,
// decoding fails ("A hardware frames reference is required..."). We always
// want CPU-side frames for sws_scale, so force the first non-hwaccel format.
static enum AVPixelFormat ForceSoftwarePixelFormat(AVCodecContext *ctx, const enum AVPixelFormat *fmts) {
    (void)ctx;
    for (const enum AVPixelFormat *p = fmts; *p != AV_PIX_FMT_NONE; p++) {
        const AVPixFmtDescriptor *desc = av_pix_fmt_desc_get(*p);
        if (desc && !(desc->flags & AV_PIX_FMT_FLAG_HWACCEL)) return *p;
    }
    return AV_PIX_FMT_NONE;
}

static bool SetErr(char *err, size_t cap, const char *fmt, ...) {
    if (err && cap) {
        va_list ap;
        va_start(ap, fmt);
        vsnprintf(err, cap, fmt, ap);
        va_end(ap);
    }
    return false;
}

// ---------------------------------------------------------------------------
// Video decode
// ---------------------------------------------------------------------------

struct PssVideoDecoder {
    const uint8_t *esData;
    uint32_t       esSize;
    uint32_t       cursor;

    const AVCodec         *codec;
    AVCodecContext        *ctx;
    AVCodecParserContext  *parser;
    AVFrame               *frame;
    AVPacket              *pkt;
    struct SwsContext     *sws;

    uint8_t *rgba;
    int      rgbaW, rgbaH;
    double   fps;
    bool     fpsKnown;
};

PssVideoDecoder *PssVideoDecoder_Create(const uint8_t *esData, uint32_t esSize) {
    if (!esData || !esSize) return NULL;

    PssVideoDecoder *dec = (PssVideoDecoder *)calloc(1, sizeof(*dec));
    if (!dec) return NULL;
    dec->esData = esData;
    dec->esSize = esSize;

    dec->codec = avcodec_find_decoder(AV_CODEC_ID_MPEG2VIDEO);
    dec->ctx    = dec->codec ? avcodec_alloc_context3(dec->codec) : NULL;
    dec->parser = av_parser_init(AV_CODEC_ID_MPEG2VIDEO);
    if (dec->ctx) dec->ctx->get_format = ForceSoftwarePixelFormat;

    if (!dec->ctx || !dec->parser || avcodec_open2(dec->ctx, dec->codec, NULL) < 0) {
        if (dec->parser) av_parser_close(dec->parser);
        if (dec->ctx) avcodec_free_context(&dec->ctx);
        free(dec);
        return NULL;
    }

    dec->frame = av_frame_alloc();
    dec->pkt   = av_packet_alloc();
    dec->fps   = 25.0;
    return dec;
}

void PssVideoDecoder_Destroy(PssVideoDecoder *dec) {
    if (!dec) return;
    if (dec->sws) sws_freeContext(dec->sws);
    av_frame_free(&dec->frame);
    av_packet_free(&dec->pkt);
    if (dec->parser) av_parser_close(dec->parser);
    if (dec->ctx) avcodec_free_context(&dec->ctx);
    free(dec->rgba);
    free(dec);
}

bool PssVideoDecoder_NextFrame(PssVideoDecoder *dec, const uint8_t **outRgba, int *outW, int *outH) {
    if (!dec) return false;

    for (;;) {
        int ret = avcodec_receive_frame(dec->ctx, dec->frame);
        if (ret == 0) {
            int w = dec->frame->width, h = dec->frame->height;
            if (w <= 0 || h <= 0) { av_frame_unref(dec->frame); continue; }

            if (!dec->rgba || dec->rgbaW != w || dec->rgbaH != h) {
                free(dec->rgba);
                dec->rgba  = (uint8_t *)malloc((size_t)w * (size_t)h * 4u);
                dec->rgbaW = w;
                dec->rgbaH = h;
                if (dec->sws) { sws_freeContext(dec->sws); dec->sws = NULL; }
            }
            if (!dec->sws) {
                dec->sws = sws_getContext(w, h, (enum AVPixelFormat)dec->frame->format,
                                          w, h, AV_PIX_FMT_RGBA, SWS_BILINEAR, NULL, NULL, NULL);
            }
            if (!dec->rgba || !dec->sws) { av_frame_unref(dec->frame); return false; }

            uint8_t *dstSlices[1] = { dec->rgba };
            int      dstStride[1] = { w * 4 };
            sws_scale(dec->sws, (const uint8_t *const *)dec->frame->data, dec->frame->linesize,
                     0, h, dstSlices, dstStride);

            if (!dec->fpsKnown) {
                AVRational fr = dec->ctx->framerate;
                if (fr.num > 0 && fr.den > 0) dec->fps = av_q2d(fr);
                dec->fpsKnown = true;
            }

            *outRgba = dec->rgba; *outW = w; *outH = h;
            av_frame_unref(dec->frame);
            return true;
        }
        if (ret != AVERROR(EAGAIN)) return false;   // real EOF or error

        if (dec->cursor >= dec->esSize) {
            avcodec_send_packet(dec->ctx, NULL);     // enter draining mode
            continue;
        }

        uint8_t *data = NULL;
        int      size = 0;
        int consumed = av_parser_parse2(dec->parser, dec->ctx, &data, &size,
                                        dec->esData + dec->cursor, (int)(dec->esSize - dec->cursor),
                                        AV_NOPTS_VALUE, AV_NOPTS_VALUE, 0);
        if (consumed > 0) dec->cursor += (uint32_t)consumed;
        else dec->cursor = dec->esSize;              // parser stalled - force EOF

        if (size > 0) {
            av_packet_unref(dec->pkt);
            dec->pkt->data = data;
            dec->pkt->size = size;
            avcodec_send_packet(dec->ctx, dec->pkt);
        }
    }
}

void PssVideoDecoder_Rewind(PssVideoDecoder *dec) {
    if (!dec) return;
    avcodec_flush_buffers(dec->ctx);
    if (dec->parser) av_parser_close(dec->parser);
    dec->parser = av_parser_init(AV_CODEC_ID_MPEG2VIDEO);
    dec->cursor = 0;
}

double PssVideoDecoder_FrameRate(const PssVideoDecoder *dec) { return dec ? dec->fps : 25.0; }

// ---------------------------------------------------------------------------
// Video encode (RGBA source -> MPEG2 elementary stream)
// ---------------------------------------------------------------------------

struct PssMpeg2Encoder {
    AVCodecContext    *ctx;
    struct SwsContext *sws;
    AVFrame           *frame;
    AVPacket          *pkt;
    uint8_t           *es;     // stb_ds dynamic array
    int64_t            pts;
    int                width, height;
    bool               failed;
};

static void FreeEncoder(PssMpeg2Encoder *enc) {
    if (!enc) return;
    arrfree(enc->es);
    if (enc->pkt) av_packet_free(&enc->pkt);
    if (enc->frame) av_frame_free(&enc->frame);
    if (enc->sws) sws_freeContext(enc->sws);
    if (enc->ctx) avcodec_free_context(&enc->ctx);
    free(enc);
}

PssMpeg2Encoder *PssMpeg2Encoder_Create(int width, int height, double fps, int sarNum, int sarDen,
                                        char *outError, size_t errorCap) {
    const AVCodec *codec = avcodec_find_encoder(AV_CODEC_ID_MPEG2VIDEO);
    if (!codec) { SetErr(outError, errorCap, "No MPEG2 video encoder available in this ffmpeg build"); return NULL; }

    PssMpeg2Encoder *enc = (PssMpeg2Encoder *)calloc(1, sizeof(*enc));
    enc->width = width;
    enc->height = height;
    enc->ctx = avcodec_alloc_context3(codec);
    if (!enc->ctx) { SetErr(outError, errorCap, "Out of memory allocating the MPEG2 encoder"); FreeEncoder(enc); return NULL; }

    AVRational fr = av_d2q(fps > 0.0 ? fps : 30.0, 100000);
    enc->ctx->width      = width;
    enc->ctx->height     = height;
    enc->ctx->pix_fmt    = AV_PIX_FMT_YUV420P;
    enc->ctx->time_base  = (AVRational){ fr.den, fr.num };
    enc->ctx->framerate  = fr;
    enc->ctx->gop_size   = 15;
    enc->ctx->max_b_frames = 0;   // keep decode order == display order for our simple player
    enc->ctx->bit_rate   = 9000000; 
    enc->ctx->sample_aspect_ratio = (AVRational){ sarNum > 0 ? sarNum : 1, sarDen > 0 ? sarDen : 1 };

    // Without these, libavcodec's mpeg2video encoder writes a stream that
    // looks nothing like a real retail PSS at the sequence-header level:
    // progressive_sequence comes out 1 (every retail sample uses 0 - the
    // PS2's decoder may only actually be validated against that), and with
    // no rc_max_rate/rc_buffer_size set, bit_rate_value in the header ends
    // up as the MPEG2 "unspecified/VBR" sentinel (0x3FFFF) and every
    // picture's vbv_delay as its sentinel (0xFFFF) instead of a real value
    // - retail files always declare a concrete rate and per-picture delay.
    // AV_CODEC_FLAG_INTERLACED_DCT is what actually flips
    // progressive_sequence to 0 (empirically verified, not documented to
    // do this); rc_max_rate/rc_min_rate pin the encoder to the declared
    // bit_rate so it always has a concrete number to write; rc_buffer_size
    // mirrors retail files' own buffer-to-bitrate ratio (~0.2s) - much
    // higher and libavcodec silently falls back to the VBR sentinel again,
    // much lower and it refuses to open at all.
    enc->ctx->flags        |= AV_CODEC_FLAG_INTERLACED_DCT;
    enc->ctx->rc_max_rate   = enc->ctx->bit_rate;
    enc->ctx->rc_min_rate   = enc->ctx->bit_rate;
    enc->ctx->rc_buffer_size = (int)(enc->ctx->bit_rate / 5);

    // Every retail sample's picture_coding_extension declares
    // intra_dc_precision=1 (9-bit), q_scale_type=1 (non-linear quantizer),
    // intra_vlc_format=1 (the alternate intra VLC tables) and
    // alternate_scan=1 - libavcodec's defaults are all the opposite (0).
    // These select which VLC tables and DCT coefficient scan order the
    // decoder must use, not just informational hints, so getting them wrong
    // corrupts every intra-coded block: exactly the "starts fine, degrades
    // into garbage/black" corruption this was causing in-game. non_linear_quant
    // additionally requires qmax<=28 to open at all.
    av_opt_set_int(enc->ctx->priv_data, "intra_dc_precision", 1, 0);
    av_opt_set_int(enc->ctx->priv_data, "non_linear_quant", 1, 0);
    av_opt_set_int(enc->ctx->priv_data, "intra_vlc", 1, 0);
    av_opt_set_int(enc->ctx->priv_data, "alternate_scan", 1, 0);
    enc->ctx->qmax = 28;

    if (avcodec_open2(enc->ctx, codec, NULL) < 0) { SetErr(outError, errorCap, "Could not open the MPEG2 encoder"); FreeEncoder(enc); return NULL; }

    enc->sws = sws_getContext(width, height, AV_PIX_FMT_RGBA, width, height, AV_PIX_FMT_YUV420P,
                              SWS_BILINEAR, NULL, NULL, NULL);
    enc->frame = av_frame_alloc();
    if (!enc->sws || !enc->frame) { SetErr(outError, errorCap, "Out of memory setting up the video encoder"); FreeEncoder(enc); return NULL; }
    enc->frame->format = AV_PIX_FMT_YUV420P;
    enc->frame->width  = width;
    enc->frame->height = height;
    if (av_frame_get_buffer(enc->frame, 32) < 0) { SetErr(outError, errorCap, "Out of memory allocating encode frame buffer"); FreeEncoder(enc); return NULL; }

    enc->pkt = av_packet_alloc();
    if (!enc->pkt) { SetErr(outError, errorCap, "Out of memory encoding video"); FreeEncoder(enc); return NULL; }
    return enc;
}

static bool DrainEncoder(PssMpeg2Encoder *enc) {
    for (;;) {
        int ret = avcodec_receive_packet(enc->ctx, enc->pkt);
        if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) return true;
        if (ret < 0) return false;
        size_t old = arrlenu(enc->es);
        arrsetlen(enc->es, old + (size_t)enc->pkt->size);
        memcpy(enc->es + old, enc->pkt->data, (size_t)enc->pkt->size);
        av_packet_unref(enc->pkt);
    }
}

bool PssMpeg2Encoder_PushRgba(PssMpeg2Encoder *enc, const uint8_t *rgba) {
    if (!enc || enc->failed) return false;
    if (av_frame_make_writable(enc->frame) < 0) { enc->failed = true; return false; }

    const uint8_t *srcSlices[1] = { rgba };
    int            srcStride[1] = { enc->width * 4 };
    sws_scale(enc->sws, srcSlices, srcStride, 0, enc->height, enc->frame->data, enc->frame->linesize);
    enc->frame->pts = enc->pts++;

    if (avcodec_send_frame(enc->ctx, enc->frame) < 0) { enc->failed = true; return false; }
    if (!DrainEncoder(enc)) { enc->failed = true; return false; }
    return true;
}

// Every retail sample declares a sequence_display_extension of exactly
// these bytes (video_format=2, color_description with primaries/transfer/
// matrix = 4/4/5, display_horizontal_size=720, display_vertical_size=480)
// right after every sequence_extension in the stream - and always these
// same values, even though vivendi.pss's own coded picture is 640x480 and
// B01_A.pss's is 512x288. So this isn't derived from the video's own coded
// size; it looks like a fixed convention this game's authoring tools
// always used (720x480 is standard NTSC broadcast/D1 resolution). Custom
// imports have no equivalent extension at all: libavcodec's own auto mode
// only computes it from the coded size (which is wrong here) and exposes
// no way to override the values directly, so this is inserted as a
// byte-for-byte copy of the retail convention after the fact instead. If
// the game reads this field to decide how to scale/position the decoded
// picture on screen, its absence is a strong candidate for the picture not
// filling the screen in-game despite decoding and playing correctly.
static uint8_t *InsertSequenceDisplayExtensions(const uint8_t *es, size_t esLen, size_t *outLen) {
    static const uint8_t kExt[11] = { 0x00, 0x00, 0x01, 0xB5, 0x25, 0x04, 0x04, 0x05, 0x0B, 0x42, 0x0F };
    uint8_t *out = NULL;   // stb_ds array
    size_t i = 0, spanStart = 0;
    while (i + 5 <= esLen) {
        // sequence_extension: start code + ext_id (top nibble) == 1, always
        // exactly 10 bytes total (6-byte body, no variable-length content).
        if (es[i] == 0 && es[i+1] == 0 && es[i+2] == 1 && es[i+3] == 0xB5 && (es[i+4] >> 4) == 0x1) {
            size_t segEnd = (i + 10 <= esLen) ? i + 10 : esLen;
            size_t segLen = segEnd - spanStart;
            size_t old = arrlenu(out);
            arrsetlen(out, old + segLen);
            memcpy(out + old, es + spanStart, segLen);
            if (segEnd - i == 10) {
                size_t old2 = arrlenu(out);
                arrsetlen(out, old2 + sizeof(kExt));
                memcpy(out + old2, kExt, sizeof(kExt));
            }
            i = spanStart = segEnd;
        } else {
            i++;
        }
    }
    size_t old = arrlenu(out);
    arrsetlen(out, old + (esLen - spanStart));
    memcpy(out + old, es + spanStart, esLen - spanStart);
    *outLen = arrlenu(out);
    return out;
}

bool PssMpeg2Encoder_Finish(PssMpeg2Encoder *enc, TwinStudio_Arena *arena, uint8_t **outEs, uint32_t *outEsSize) {
    if (!enc) return false;
    bool ok = !enc->failed;
    if (ok) {
        avcodec_send_frame(enc->ctx, NULL);   // flush
        ok = DrainEncoder(enc) && arrlenu(enc->es) > 0;
    }
    if (ok) {
        size_t patchedLen;
        uint8_t *patched = InsertSequenceDisplayExtensions(enc->es, arrlenu(enc->es), &patchedLen);
        arrfree(enc->es);
        enc->es = patched;
    }
    if (ok) {
        // libavcodec's mpeg2video encoder never emits sequence_end_code on
        // its own - every retail sample's video ES ends with exactly these
        // 4 bytes as its literal last bytes, and without it the decoder has
        // no explicit "no more pictures are coming" signal, only running out
        // of data to read. That's very plausibly why custom imports never
        // reached a clean end-of-playback state in-game even after the
        // video itself decoded and displayed correctly.
        static const uint8_t kSequenceEndCode[4] = { 0x00, 0x00, 0x01, 0xB7 };
        size_t old = arrlenu(enc->es);
        arrsetlen(enc->es, old + sizeof(kSequenceEndCode));
        memcpy(enc->es + old, kSequenceEndCode, sizeof(kSequenceEndCode));
    }
    if (ok) {
        *outEsSize = (uint32_t)arrlenu(enc->es);
        *outEs     = (uint8_t *)TwinStudio_ArenaAlloc(arena, *outEsSize);
        memcpy(*outEs, enc->es, *outEsSize);
    }
    FreeEncoder(enc);
    return ok;
}

void PssMpeg2Encoder_Abort(PssMpeg2Encoder *enc) { FreeEncoder(enc); }

// ---------------------------------------------------------------------------
// Audio: SPU2-ADPCM (VAG) / PCM <-> interleaved S16 PCM
// ---------------------------------------------------------------------------

bool PssAudioTrack_DecodeToPcm(const PssAudioTrack *track, TwinStudio_Arena *arena,
                               int16_t **outPcm, uint32_t *outFrameCount) {
    if (!track || !track->present || !track->data || track->dataSize == 0 || track->channels == 0) return false;

    if (track->type == PSS_AUDIO_VAG_ADPCM) {
        uint32_t interleaveParam = (track->channels >= 2) ? track->interleave : 0;
        TwinStudio_AdpcmDecodeResult res = TwinStudio_AdpcmDecode(arena, track->data, track->dataSize, interleaveParam);
        if (!res.pcmData || res.pcmDataSize == 0) return false;
        *outPcm = (int16_t *)res.pcmData;
        *outFrameCount = (uint32_t)(res.pcmDataSize / (2u * track->channels));
        return true;
    }

    // PCM16, big or little endian. Like SPU2-ADPCM, a multi-channel PCM
    // track is block-interleaved rather than sample-interleaved: `interleave`
    // bytes of channel 0, then `interleave` bytes of channel 1, and so on,
    // repeating (that's what the SShd "interleave size" field is for - a
    // flat sample-interleaved PCM stream wouldn't need one). Verified
    // against a real retail file: reading it as flat left/right gives two
    // near-identical "channels", a giveaway that the reader is slicing
    // through the middle of same-channel blocks rather than across channels.
    bool swapEndian = (track->type == PSS_AUDIO_PCM16_BE);

    if (track->channels == 1 || track->interleave == 0) {
        uint32_t frames = track->dataSize / (2u * track->channels);
        if (frames == 0) return false;
        uint32_t sampleCount = frames * track->channels;

        int16_t *pcm = (int16_t *)TwinStudio_ArenaAlloc(arena, (size_t)sampleCount * sizeof(int16_t));
        if (!swapEndian) {
            memcpy(pcm, track->data, (size_t)sampleCount * sizeof(int16_t));
        } else {
            const uint8_t *src = track->data;
            for (uint32_t i = 0; i < sampleCount; i++) {
                pcm[i] = (int16_t)((uint16_t)(src[i * 2] << 8) | (uint16_t)src[i * 2 + 1]);
            }
        }
        *outPcm = pcm;
        *outFrameCount = frames;
        return true;
    }

    uint32_t samplesPerChunk = track->interleave / 2u;
    if (samplesPerChunk == 0) return false;
    size_t groupBytes = (size_t)track->interleave * track->channels;
    size_t numGroups  = track->dataSize / groupBytes;
    if (numGroups == 0) return false;

    uint32_t frames = (uint32_t)(numGroups * samplesPerChunk);
    int16_t *pcm = (int16_t *)TwinStudio_ArenaAlloc(arena, (size_t)frames * track->channels * sizeof(int16_t));

    for (size_t g = 0; g < numGroups; g++) {
        const uint8_t *groupBase = track->data + g * groupBytes;
        for (uint32_t s = 0; s < samplesPerChunk; s++) {
            for (uint32_t c = 0; c < track->channels; c++) {
                const uint8_t *sp = groupBase + (size_t)c * track->interleave + (size_t)s * 2u;
                uint16_t raw = swapEndian ? (uint16_t)((sp[0] << 8) | sp[1]) : (uint16_t)(sp[0] | (sp[1] << 8));
                pcm[((size_t)g * samplesPerChunk + s) * track->channels + c] = (int16_t)raw;
            }
        }
    }

    *outPcm = pcm;
    *outFrameCount = frames;
    return true;
}

typedef struct {
    const int16_t *pcm;
    uint32_t       frameCount;
    uint32_t       channels;
    uint32_t       channelIndex;
    uint32_t       pos;
} PcmChannelReader;

static int32_t ReadPcmChannel(void *priv, double *out, int32_t len) {
    PcmChannelReader *r = (PcmChannelReader *)priv;
    int32_t got = 0;
    for (int32_t i = 0; i < len; i++) {
        if (r->pos < r->frameCount) {
            out[i] = (double)r->pcm[r->pos * r->channels + r->channelIndex];
            r->pos++;
            got++;
        } else {
            out[i] = 0.0;
        }
    }
    return got;
}

typedef struct {
    uint8_t *dst;   // blocksPerChannel * 16 bytes
    uint32_t pos;   // blocks written so far
} AdpcmBlockWriter;

static int32_t WriteAdpcmBlock(void *priv, void *data, int32_t len) {
    AdpcmBlockWriter *w = (AdpcmBlockWriter *)priv;
    memcpy(w->dst + (size_t)w->pos * 16u, data, (size_t)len);
    w->pos++;
    return len;
}

static bool EncodePcm16(TwinStudio_Arena *arena, const int16_t *pcm, uint32_t frameCount,
                         uint32_t sampleRate, uint32_t channels, PssAudioType type,
                         PssAudioTrack *outTrack) {
    bool swapEndian = (type == PSS_AUDIO_PCM16_BE);
    const uint32_t interleave = 512;

    uint32_t dataSize;
    uint8_t *data;

    if (channels == 1) {
        dataSize = frameCount * 2u;
        data = (uint8_t *)TwinStudio_ArenaAlloc(arena, dataSize);
        if (!swapEndian) {
            memcpy(data, pcm, dataSize);
        } else {
            for (uint32_t i = 0; i < frameCount; i++) {
                uint16_t v = (uint16_t)pcm[i];
                data[i * 2]     = (uint8_t)(v >> 8);
                data[i * 2 + 1] = (uint8_t)(v & 0xFF);
            }
        }
    } else {
        // Inverse of PssAudioTrack_DecodeToPcm's block-interleave path:
        // groups of `interleave` bytes of channel 0 then `interleave` bytes
        // of channel 1, repeating.
        uint32_t samplesPerChunk = interleave / 2u;
        uint32_t numGroups = (frameCount + samplesPerChunk - 1u) / samplesPerChunk;
        uint32_t paddedFrames = numGroups * samplesPerChunk;
        size_t groupBytes = (size_t)interleave * channels;
        dataSize = (uint32_t)(numGroups * groupBytes);
        data = (uint8_t *)TwinStudio_ArenaAlloc(arena, dataSize);
        memset(data, 0, dataSize);

        for (uint32_t g = 0; g < numGroups; g++) {
            uint8_t *groupBase = data + (size_t)g * groupBytes;
            for (uint32_t s = 0; s < samplesPerChunk; s++) {
                uint32_t frame = g * samplesPerChunk + s;
                for (uint32_t c = 0; c < channels; c++) {
                    int16_t sample = (frame < frameCount) ? pcm[frame * channels + c] : 0;
                    uint16_t v = (uint16_t)sample;
                    uint8_t *dp = groupBase + (size_t)c * interleave + (size_t)s * 2u;
                    if (swapEndian) { dp[0] = (uint8_t)(v >> 8); dp[1] = (uint8_t)(v & 0xFF); }
                    else             { dp[0] = (uint8_t)(v & 0xFF); dp[1] = (uint8_t)(v >> 8); }
                }
            }
        }
        (void)paddedFrames;
    }

    memset(outTrack, 0, sizeof(*outTrack));
    outTrack->present        = true;
    outTrack->type           = type;
    outTrack->sampleRate     = sampleRate;
    outTrack->channels       = channels;
    outTrack->interleave     = (channels >= 2) ? interleave : 0;
    outTrack->loopStartBlock = -1;
    outTrack->loopEndBlock   = -1;
    outTrack->data           = data;
    outTrack->dataSize       = dataSize;
    return true;
}

bool PssAudioTrack_EncodeFromPcm(TwinStudio_Arena *arena, const int16_t *pcm, uint32_t frameCount,
                                 uint32_t sampleRate, uint32_t channels, PssAudioType type,
                                 PssAudioTrack *outTrack) {
    if (!pcm || frameCount == 0 || (channels != 1 && channels != 2)) return false;

    if (type == PSS_AUDIO_PCM16_LE || type == PSS_AUDIO_PCM16_BE) {
        return EncodePcm16(arena, pcm, frameCount, sampleRate, channels, type, outTrack);
    }

    const uint32_t blocksPerChannel = (frameCount + 27u) / 28u;
    const uint32_t groupBlocks      = 512u / 16u;   // SShd interleave size is fixed at 512 bytes
    const uint32_t paddedBlocks     = (channels == 2)
        ? ((blocksPerChannel + groupBlocks - 1u) / groupBlocks) * groupBlocks
        : blocksPerChannel;

    uint8_t *chan[2] = { NULL, NULL };
    for (uint32_t c = 0; c < channels; c++) {
        chan[c] = (uint8_t *)TWIN_MALLOC((size_t)paddedBlocks * 16u);
        memset(chan[c], 0, (size_t)paddedBlocks * 16u);

        PcmChannelReader reader = { pcm, frameCount, channels, c, 0 };
        AdpcmBlockWriter writer = { chan[c], 0 };

        TwinStudio_Arena setupArena = TwinStudio_CreateArena(sizeof(TwinStudio_AdpcmSetup) + 64);
        TwinStudio_AdpcmSetup *setup = TwinStudio_AdpcmCreate(&setupArena, ReadPcmChannel, &reader,
                                                               WriteAdpcmBlock, &writer, -1);
        TwinStudio_AdpcmEncode(setup, (int32_t)blocksPerChannel);
        TwinStudio_ArenaFree(&setupArena);

        // TwinStudio_AdpcmEncode already flags the true last block as
        // end-of-stream itself when frameCount isn't a multiple of 28 (its
        // GetPCM callback returns fewer than 28 samples on that call). The
        // mono decoder skips a flagged block's samples entirely rather than
        // reading through it, so touching that flag here would drop real
        // trailing audio instead of just the silence padding this channel
        // out to a whole 512-byte interleave group - leave it alone and let
        // the group padding decode as a few extra silent samples instead.
    }

    uint32_t dataSize = (channels == 2) ? (paddedBlocks * 16u * 2u) : (paddedBlocks * 16u);
    uint8_t *data = (uint8_t *)TwinStudio_ArenaAlloc(arena, dataSize);

    if (channels == 1) {
        memcpy(data, chan[0], dataSize);
    } else {
        uint32_t numGroups = paddedBlocks / groupBlocks;
        uint8_t *dst = data;
        for (uint32_t g = 0; g < numGroups; g++) {
            memcpy(dst, chan[0] + (size_t)g * groupBlocks * 16u, (size_t)groupBlocks * 16u);
            dst += (size_t)groupBlocks * 16u;
            memcpy(dst, chan[1] + (size_t)g * groupBlocks * 16u, (size_t)groupBlocks * 16u);
            dst += (size_t)groupBlocks * 16u;
        }
    }

    for (uint32_t c = 0; c < channels; c++) TWIN_FREE(chan[c]);

    memset(outTrack, 0, sizeof(*outTrack));
    outTrack->present        = true;
    outTrack->type           = type;
    outTrack->sampleRate     = sampleRate;
    outTrack->channels       = channels;
    outTrack->interleave     = 512;
    outTrack->loopStartBlock = -1;
    outTrack->loopEndBlock   = -1;
    outTrack->data           = data;
    outTrack->dataSize       = dataSize;
    return true;
}
