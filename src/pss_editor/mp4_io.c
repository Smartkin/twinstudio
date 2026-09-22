#include "mp4_io.h"

#include <limits.h>
#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <stb_ds.h>

#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/channel_layout.h>
#include <libavutil/mathematics.h>
#include <libavutil/opt.h>
#include <libavutil/pixdesc.h>
#include <libswresample/swresample.h>
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

// avcodec_get_supported_config() is the non-deprecated replacement for
// reading AVCodec::pix_fmts/sample_fmts directly.
static enum AVPixelFormat PickPixFmt(const AVCodec *codec, enum AVPixelFormat fallback) {
    const void *configs = NULL;
    int count = 0;
    if (avcodec_get_supported_config(NULL, codec, AV_CODEC_CONFIG_PIX_FORMAT, 0, &configs, &count) >= 0 &&
        configs && count > 0) {
        return ((const enum AVPixelFormat *)configs)[0];
    }
    return fallback;
}

static enum AVSampleFormat PickSampleFmt(const AVCodec *codec, enum AVSampleFormat fallback) {
    const void *configs = NULL;
    int count = 0;
    if (avcodec_get_supported_config(NULL, codec, AV_CODEC_CONFIG_SAMPLE_FORMAT, 0, &configs, &count) >= 0 &&
        configs && count > 0) {
        return ((const enum AVSampleFormat *)configs)[0];
    }
    return fallback;
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
// Import: demux+decode anything ffmpeg understands, re-encode video to
// MPEG2, decode audio (if any) to interleaved S16 PCM.
// ---------------------------------------------------------------------------

bool Mp4ProbeVideo(const char *path, int *outWidth, int *outHeight, double *outFps,
                   char *outError, size_t errorCap) {
    AVFormatContext *fmt = NULL;
    bool ok = false;

    if (avformat_open_input(&fmt, path, NULL, NULL) < 0) { SetErr(outError, errorCap, "Could not open \"%s\"", path); goto cleanup; }
    if (avformat_find_stream_info(fmt, NULL) < 0) { SetErr(outError, errorCap, "Could not read stream info from \"%s\"", path); goto cleanup; }

    {
        int videoIdx = av_find_best_stream(fmt, AVMEDIA_TYPE_VIDEO, -1, -1, NULL, 0);
        if (videoIdx < 0) { SetErr(outError, errorCap, "\"%s\" has no video stream", path); goto cleanup; }
        AVStream *vs = fmt->streams[videoIdx];
        *outWidth  = vs->codecpar->width;
        *outHeight = vs->codecpar->height;
        AVRational fpsRat = av_guess_frame_rate(fmt, vs, NULL);
        *outFps = (fpsRat.num > 0 && fpsRat.den > 0) ? av_q2d(fpsRat) : 30.0;
    }
    ok = true;

cleanup:
    if (fmt) avformat_close_input(&fmt);
    return ok;
}

// Returns the CANVAS Mp4Import will actually encode at - not necessarily
// the same shape as the source. See Mp4Import's comment for why: the
// source is stretched to fill this canvas exactly.
void Mp4ComputeImportTarget(int srcWidth, int srcHeight, double srcFps, const Mp4ImportLimits *limits,
                            int *outWidth, int *outHeight, double *outFps) {
    int maxW = limits ? limits->maxWidth : 0;
    int maxH = limits ? limits->maxHeight : 0;
    double maxFps = limits ? limits->maxFps : 0.0;
    *outFps = (maxFps > 0.0 && maxFps < srcFps) ? maxFps : srcFps;

    if (limits && limits->exact && maxW > 0 && maxH > 0) {
        // Matching a specific already-loaded PSS's own resolution exactly -
        // that resolution is already known-good (it came from a real file).
        *outWidth  = maxW;
        *outHeight = maxH;
        return;
    }

    // Every real retail PSS resolution seen is an EXACT 4:3 (640x480) or
    // EXACT 16:9 (512x288) ratio, multiple-of-16 in both dimensions - not
    // merely close to one. Naively scaling the source to fit within maxW x
    // maxH and then flooring each axis independently to a multiple of 16
    // (as this used to do) preserves the source's own aspect ratio, but
    // for anything not already exactly 4:3 a mult-16 source generally
    // lands close to but not exactly on a standard ratio (e.g. a 16:9
    // source under the 640-wide NTSC cap floors to 640x352, not 640x360 -
    // 351.56 is not a multiple of 16 in a way that keeps the ratio exact).
    // Declaring the correct aspect_ratio_information still requires a
    // non-square sample_aspect_ratio to correct for that residual error,
    // and empirically the game does not apply it: video imported that way
    // renders undersized instead of filling the screen, even though the
    // resulting MPEG2 is fully spec-conformant. Quantizing to the largest
    // multiple of a ratio-preserving step (64x48 for 4:3, 256x144 for
    // 16:9) that fits within the cap guarantees an exact ratio - square
    // pixels, no correction needed - the same way every real sample
    // happens to be exact. 512x288 (step 256x144, k=2) is exactly what a
    // 16:9 source lands on under the PAL cap, which is why it works; the
    // NTSC cap's 640 width isn't a multiple of 256, so it settles for the
    // same 512x288 (k=2) rather than the imprecise 640x352.
    bool widescreen;
    if (limits && limits->aspect == PSS_ASPECT_4_3)       widescreen = false;
    else if (limits && limits->aspect == PSS_ASPECT_16_9) widescreen = true;
    else {
        double srcAspect = (double)srcWidth / (double)srcHeight;
        widescreen = fabs(srcAspect - 16.0 / 9.0) < fabs(srcAspect - 4.0 / 3.0);
    }
    int stepW = widescreen ? 256 : 64;
    int stepH = widescreen ? 144 : 48;

    int wCap = (maxW > 0 && maxW < srcWidth) ? maxW : srcWidth;
    int hCap = (maxH > 0 && maxH < srcHeight) ? maxH : srcHeight;
    int k  = wCap / stepW;
    int kh = hCap / stepH;
    if (kh < k) k = kh;

    if (k >= 1) {
        *outWidth  = stepW * k;
        *outHeight = stepH * k;
    } else {
        // Degenerate case: caps smaller than a single quantization step.
        // Fall back to independently-floored dimensions (not exact-ratio,
        // but still mult-16 and never upscaled) rather than exceed the cap.
        double sx = (maxW > 0 && srcWidth > maxW) ? (double)maxW / srcWidth : 1.0;
        double sy = (maxH > 0 && srcHeight > maxH) ? (double)maxH / srcHeight : 1.0;
        double scale = sx < sy ? sx : sy;
        int w = (int)(srcWidth * scale) & ~15;
        int h = (int)(srcHeight * scale) & ~15;
        *outWidth  = w < 16 ? 16 : w;
        *outHeight = h < 16 ? 16 : h;
    }
}

void Mp4ComputeSampleAspectRatio(int srcWidth, int srcHeight, int outWidth, int outHeight,
                                 int *outSarNum, int *outSarDen) {
    double srcAspect = (double)srcWidth / (double)srcHeight;
    double dar43 = 4.0 / 3.0, dar169 = 16.0 / 9.0;
    int darNum, darDen;
    if (fabs(srcAspect - dar169) < fabs(srcAspect - dar43)) { darNum = 16; darDen = 9; }
    else                                                    { darNum = 4;  darDen = 3; }

    // sample_aspect_ratio (pixel aspect) is whatever makes
    // coded_size * SAR equal the target display aspect ratio exactly:
    // SAR = DAR / (outWidth/outHeight) = (darNum*outHeight) / (darDen*outWidth).
    av_reduce(outSarNum, outSarDen, (int64_t)darNum * outHeight, (int64_t)darDen * outWidth, INT_MAX);
}

// Decimates a source frame sequence down to a lower output frame rate by
// dropping frames at even intervals, e.g. 30fps -> 25fps keeps 25 of every
// 30 source frames spread evenly rather than dropping every 6th outright.
typedef struct { double ratio, next; uint32_t srcIndex; } FpsDecimator;

static FpsDecimator FpsDecimatorInit(double srcFps, double dstFps) {
    FpsDecimator d = { .ratio = 1.0, .next = 0.0, .srcIndex = 0 };
    if (dstFps > 0.0 && dstFps < srcFps) d.ratio = srcFps / dstFps;
    return d;
}
static bool FpsDecimatorKeep(FpsDecimator *d) {
    bool keep = (double)d->srcIndex >= d->next;
    if (keep) d->next += d->ratio;
    d->srcIndex++;
    return keep;
}

bool Mp4Import(const char *path, TwinStudio_Arena *arena, const Mp4ImportLimits *limits, Mp4ImportResult *out,
              PssProgressFn progress, void *progressUser, char *outError, size_t errorCap) {
    memset(out, 0, sizeof(*out));
    bool ok = false;

    AVFormatContext   *fmt      = NULL;
    AVCodecContext    *vctx     = NULL;
    AVCodecContext    *actx     = NULL;
    struct SwsContext *sws      = NULL;
    struct SwrContext *swr      = NULL;
    AVFrame            *frame    = NULL;
    AVPacket           *pkt      = NULL;
    PssMpeg2Encoder    *enc      = NULL;
    uint8_t            *rgba     = NULL;
    int16_t            *tmpPcm   = NULL;
    int16_t            *pcmArr   = NULL;   // stb_ds dynamic array
    AVChannelLayout     inLayout = { 0 };
    AVChannelLayout     outLayout = { 0 };

    if (avformat_open_input(&fmt, path, NULL, NULL) < 0) { SetErr(outError, errorCap, "Could not open \"%s\"", path); goto cleanup; }
    if (avformat_find_stream_info(fmt, NULL) < 0) { SetErr(outError, errorCap, "Could not read stream info from \"%s\"", path); goto cleanup; }

    int videoIdx = av_find_best_stream(fmt, AVMEDIA_TYPE_VIDEO, -1, -1, NULL, 0);
    int audioIdx = av_find_best_stream(fmt, AVMEDIA_TYPE_AUDIO, -1, -1, NULL, 0);
    if (videoIdx < 0) { SetErr(outError, errorCap, "\"%s\" has no video stream", path); goto cleanup; }

    AVStream *vs = fmt->streams[videoIdx];
    const AVCodec *vdec = avcodec_find_decoder(vs->codecpar->codec_id);
    if (!vdec) { SetErr(outError, errorCap, "No decoder available for this video codec"); goto cleanup; }
    vctx = avcodec_alloc_context3(vdec);
    avcodec_parameters_to_context(vctx, vs->codecpar);
    vctx->get_format = ForceSoftwarePixelFormat;
    if (avcodec_open2(vctx, vdec, NULL) < 0) { SetErr(outError, errorCap, "Could not open the video decoder"); goto cleanup; }

    int width = vs->codecpar->width, height = vs->codecpar->height;
    AVRational fpsRat = av_guess_frame_rate(fmt, vs, NULL);
    double fps = (fpsRat.num > 0 && fpsRat.den > 0) ? av_q2d(fpsRat) : 30.0;

    // `canvasWidth`/`canvasHeight` is what actually gets encoded - always an
    // exact 4:3 or 16:9 multiple-of-16 shape (see Mp4ComputeImportTarget).
    // `contentWidth`/`contentHeight` is the source picture itself within
    // that canvas: with PSS_FIT_STRETCH (the default) it's the same as the
    // canvas (non-uniform scale, source aspect ratio not preserved); with
    // PSS_FIT_LETTERBOX it's scaled to fit the canvas preserving the
    // source's own aspect ratio and centered, with black bars filling
    // whatever's left over. Either way the canvas ends up completely
    // filled - replacing a PSS whose video only fills part of whatever
    // on-screen area the game reserves for that specific asset leaves the
    // rest of that area as untouched black space, and the game does not
    // appear to dynamically resize that area to match an oddly-sized
    // replacement.
    int canvasWidth, canvasHeight;
    double outFps;
    Mp4ComputeImportTarget(width, height, fps, limits, &canvasWidth, &canvasHeight, &outFps);
    FpsDecimator decimator = FpsDecimatorInit(fps, outFps);

    int contentWidth = canvasWidth, contentHeight = canvasHeight;
    if (limits && limits->fit == PSS_FIT_LETTERBOX) {
        double fitScale = (double)canvasWidth / width < (double)canvasHeight / height
                             ? (double)canvasWidth / width : (double)canvasHeight / height;
        if (fitScale > 1.0) fitScale = 1.0;   // never upscale
        contentWidth  = (int)(width  * fitScale) & ~1;
        contentHeight = (int)(height * fitScale) & ~1;
        if (contentWidth  < 2) contentWidth  = 2;
        if (contentHeight < 2) contentHeight = 2;
    }
    int contentOffX = ((canvasWidth  - contentWidth)  / 2) & ~1;
    int contentOffY = ((canvasHeight - contentHeight) / 2) & ~1;

    // The canvas (content, or content plus letterbox bars) is what should
    // display at the target aspect ratio, not the source - in letterbox
    // mode the bars are as much "the picture" as the content is, not
    // something to crop away. Since the canvas is always an exact 4:3/16:9
    // ratio by construction, this comes out SAR 1:1 (square pixels) in
    // practice - no correction needed.
    int sarNum, sarDen;
    Mp4ComputeSampleAspectRatio(canvasWidth, canvasHeight, canvasWidth, canvasHeight, &sarNum, &sarDen);

    uint32_t outSampleRate = 0, outChannels = 0;
    if (audioIdx >= 0) {
        AVStream *as = fmt->streams[audioIdx];
        const AVCodec *adec = avcodec_find_decoder(as->codecpar->codec_id);
        if (adec) {
            actx = avcodec_alloc_context3(adec);
            avcodec_parameters_to_context(actx, as->codecpar);
            if (avcodec_open2(actx, adec, NULL) < 0) { avcodec_free_context(&actx); actx = NULL; }
        }
        if (actx) {
            outSampleRate = actx->sample_rate > 0 ? (uint32_t)actx->sample_rate : 48000;
            outChannels   = (actx->ch_layout.nb_channels >= 2) ? 2u : 1u;
            av_channel_layout_default(&outLayout, (int)outChannels);
            av_channel_layout_copy(&inLayout, &actx->ch_layout);
            int r = swr_alloc_set_opts2(&swr, &outLayout, AV_SAMPLE_FMT_S16, (int)outSampleRate,
                                        &inLayout, actx->sample_fmt, actx->sample_rate, 0, NULL);
            if (r < 0 || !swr || swr_init(swr) < 0) {
                if (swr) swr_free(&swr);
                avcodec_free_context(&actx);
                actx = NULL;
            }
        }
    }

    enc = PssMpeg2Encoder_Create(canvasWidth, canvasHeight, outFps, sarNum, sarDen, outError, errorCap);
    if (!enc) goto cleanup;

    // sws_scale handles the pixel format conversion and the scale (uniform
    // fit or non-uniform stretch, per contentWidth/Height above) in one
    // pass, writing into `rgba` at the content sub-rect's offset/stride
    // within the full canvas - identical to writing the whole buffer when
    // there's no letterboxing (contentOffX/Y == 0 and canvasWidth/Height ==
    // content, i.e. PSS_FIT_STRETCH).
    sws = sws_getContext(width, height, vctx->pix_fmt, contentWidth, contentHeight, AV_PIX_FMT_RGBA,
                         SWS_BILINEAR, NULL, NULL, NULL);
    frame = av_frame_alloc();
    pkt   = av_packet_alloc();
    rgba  = (uint8_t *)malloc((size_t)canvasWidth * (size_t)canvasHeight * 4u);
    if (!sws || !frame || !pkt || !rgba) { SetErr(outError, errorCap, "Out of memory importing \"%s\"", path); goto cleanup; }
    if (canvasWidth != contentWidth || canvasHeight != contentHeight) {
        // Opaque black letterbox/pillarbox bars - sws_scale only ever
        // touches the content sub-rect below, so this only needs doing once.
        uint8_t *p = rgba;
        for (size_t i = 0; i < (size_t)canvasWidth * (size_t)canvasHeight; i++, p += 4) {
            p[0] = 0; p[1] = 0; p[2] = 0; p[3] = 255;
        }
    }
    uint8_t *contentDst = rgba + ((size_t)contentOffY * (size_t)canvasWidth + (size_t)contentOffX) * 4u;
    int      contentStride = canvasWidth * 4;

    {
        int64_t fileSize = avio_size(fmt->pb);
        int packetCount = 0;
        int readRet;
        while ((readRet = av_read_frame(fmt, pkt)) >= 0) {
            if (pkt->stream_index == videoIdx) {
                if (avcodec_send_packet(vctx, pkt) == 0) {
                    while (avcodec_receive_frame(vctx, frame) == 0) {
                        if (FpsDecimatorKeep(&decimator)) {
                            uint8_t *dst[1] = { contentDst };
                            int dstStride[1] = { contentStride };
                            sws_scale(sws, (const uint8_t *const *)frame->data, frame->linesize, 0, height, dst, dstStride);
                            PssMpeg2Encoder_PushRgba(enc, rgba);
                        }
                        av_frame_unref(frame);
                    }
                }
            } else if (actx && pkt->stream_index == audioIdx) {
                if (avcodec_send_packet(actx, pkt) == 0) {
                    while (avcodec_receive_frame(actx, frame) == 0) {
                        int wanted = (int)swr_get_out_samples(swr, frame->nb_samples);
                        if (wanted > 0) {
                            free(tmpPcm);
                            tmpPcm = (int16_t *)malloc((size_t)wanted * outChannels * sizeof(int16_t));
                            uint8_t *outPlanes[1] = { (uint8_t *)tmpPcm };
                            int converted = swr_convert(swr, outPlanes, wanted,
                                                        (const uint8_t **)frame->extended_data, frame->nb_samples);
                            if (converted > 0) {
                                size_t old = arrlenu(pcmArr);
                                arrsetlen(pcmArr, old + (size_t)converted * outChannels);
                                memcpy(pcmArr + old, tmpPcm, (size_t)converted * outChannels * sizeof(int16_t));
                            }
                        }
                        av_frame_unref(frame);
                    }
                }
            }
            av_packet_unref(pkt);

            if (progress && (++packetCount % 16) == 0) {
                float frac = -1.0f;
                if (fileSize > 0) {
                    int64_t pos = avio_tell(fmt->pb);
                    frac = (float)((double)pos / (double)fileSize);
                }
                progress(progressUser, frac);
            }
        }

        // Flush both decoders.
        avcodec_send_packet(vctx, NULL);
        while (avcodec_receive_frame(vctx, frame) == 0) {
            if (FpsDecimatorKeep(&decimator)) {
                uint8_t *dst[1] = { contentDst };
                int dstStride[1] = { contentStride };
                sws_scale(sws, (const uint8_t *const *)frame->data, frame->linesize, 0, height, dst, dstStride);
                PssMpeg2Encoder_PushRgba(enc, rgba);
            }
            av_frame_unref(frame);
        }
        if (actx) {
            avcodec_send_packet(actx, NULL);
            while (avcodec_receive_frame(actx, frame) == 0) {
                int wanted = (int)swr_get_out_samples(swr, frame->nb_samples);
                if (wanted > 0) {
                    free(tmpPcm);
                    tmpPcm = (int16_t *)malloc((size_t)wanted * outChannels * sizeof(int16_t));
                    uint8_t *outPlanes[1] = { (uint8_t *)tmpPcm };
                    int converted = swr_convert(swr, outPlanes, wanted, (const uint8_t **)frame->extended_data, frame->nb_samples);
                    if (converted > 0) {
                        size_t old = arrlenu(pcmArr);
                        arrsetlen(pcmArr, old + (size_t)converted * outChannels);
                        memcpy(pcmArr + old, tmpPcm, (size_t)converted * outChannels * sizeof(int16_t));
                    }
                }
                av_frame_unref(frame);
            }
        }
    }

    if (!PssMpeg2Encoder_Finish(enc, arena, &out->videoEs, &out->videoEsSize)) {
        enc = NULL;
        SetErr(outError, errorCap, "MPEG2 re-encode of \"%s\" failed", path);
        goto cleanup;
    }
    enc = NULL;
    out->width = canvasWidth;
    out->height = canvasHeight;
    out->fps = outFps;

    if (actx && arrlenu(pcmArr) > 0) {
        size_t samples = arrlenu(pcmArr);
        out->pcmFrames  = (uint32_t)(samples / outChannels);
        out->sampleRate = outSampleRate;
        out->channels   = outChannels;
        out->pcm = (int16_t *)TwinStudio_ArenaAlloc(arena, samples * sizeof(int16_t));
        memcpy(out->pcm, pcmArr, samples * sizeof(int16_t));
    }

    ok = true;

cleanup:
    av_channel_layout_uninit(&inLayout);
    av_channel_layout_uninit(&outLayout);
    arrfree(pcmArr);
    free(tmpPcm);
    free(rgba);
    if (pkt) av_packet_free(&pkt);
    if (frame) av_frame_free(&frame);
    if (sws) sws_freeContext(sws);
    if (swr) swr_free(&swr);
    if (enc) PssMpeg2Encoder_Abort(enc);
    if (actx) avcodec_free_context(&actx);
    if (vctx) avcodec_free_context(&vctx);
    if (fmt) avformat_close_input(&fmt);
    return ok;
}

// ---------------------------------------------------------------------------
// Export: decode PSS video/audio (through pss_codec) and re-encode as an
// H.264 + AAC mp4.
// ---------------------------------------------------------------------------

bool Mp4Export(const char *path, PssVideoDecoder *videoDec, uint32_t frameCount,
              const int16_t *pcm, uint32_t pcmFrames, uint32_t sampleRate, uint32_t channels,
              PssProgressFn progress, void *progressUser, char *outError, size_t errorCap) {
    bool ok = false;

    AVFormatContext   *ofmt = NULL;
    AVCodecContext    *vctx = NULL, *actx = NULL;
    AVStream           *vst = NULL, *ast = NULL;
    struct SwsContext *sws  = NULL;
    struct SwrContext *swr  = NULL;
    AVFrame            *vframe = NULL, *aframe = NULL;
    AVPacket           *pkt = NULL;
    AVDictionary       *voptions = NULL;
    AVChannelLayout     inLayout = { 0 };

    PssVideoDecoder_Rewind(videoDec);

    const uint8_t *rgba = NULL;
    int w = 0, h = 0;
    if (!PssVideoDecoder_NextFrame(videoDec, &rgba, &w, &h)) {
        SetErr(outError, errorCap, "No video frames to export");
        goto cleanup;
    }
    double fps = PssVideoDecoder_FrameRate(videoDec);

    const AVCodec *vcodec = avcodec_find_encoder_by_name("libx264");
    if (!vcodec) vcodec = avcodec_find_encoder(AV_CODEC_ID_H264);
    if (!vcodec) { SetErr(outError, errorCap, "No H.264 encoder available in this ffmpeg build"); goto cleanup; }

    if (avformat_alloc_output_context2(&ofmt, NULL, "mp4", path) < 0 || !ofmt) {
        SetErr(outError, errorCap, "Could not create an mp4 writer for \"%s\"", path);
        goto cleanup;
    }

    vst = avformat_new_stream(ofmt, NULL);
    vctx = avcodec_alloc_context3(vcodec);
    if (!vst || !vctx) { SetErr(outError, errorCap, "Out of memory setting up video export"); goto cleanup; }
    vctx->width  = w;
    vctx->height = h;
    vctx->pix_fmt = PickPixFmt(vcodec, AV_PIX_FMT_YUV420P);
    {
        AVRational fr = av_d2q(fps > 0.0 ? fps : 30.0, 100000);
        vctx->time_base = (AVRational){ fr.den, fr.num };
        vctx->framerate = fr;
    }
    vctx->gop_size = 15;
    vctx->bit_rate = (int64_t)w * h * 4;
    if (ofmt->oformat->flags & AVFMT_GLOBALHEADER) vctx->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;
    if (strcmp(vcodec->name, "libx264") == 0) {
        av_dict_set(&voptions, "preset", "medium", 0);
        av_dict_set(&voptions, "crf", "20", 0);
    }
    if (avcodec_open2(vctx, vcodec, &voptions) < 0) { SetErr(outError, errorCap, "Could not open the H.264 encoder"); goto cleanup; }
    avcodec_parameters_from_context(vst->codecpar, vctx);
    vst->time_base = vctx->time_base;

    sws = sws_getContext(w, h, AV_PIX_FMT_RGBA, w, h, vctx->pix_fmt, SWS_BILINEAR, NULL, NULL, NULL);
    vframe = av_frame_alloc();
    if (!sws || !vframe) { SetErr(outError, errorCap, "Out of memory setting up video export"); goto cleanup; }
    vframe->format = vctx->pix_fmt;
    vframe->width  = w;
    vframe->height = h;
    if (av_frame_get_buffer(vframe, 32) < 0) { SetErr(outError, errorCap, "Out of memory allocating export frame"); goto cleanup; }

    bool haveAudio = pcm && pcmFrames > 0 && channels > 0;
    int audioFrameSize = 0;
    if (haveAudio) {
        const AVCodec *acodec = avcodec_find_encoder(AV_CODEC_ID_AAC);
        if (acodec) {
            ast  = avformat_new_stream(ofmt, NULL);
            actx = avcodec_alloc_context3(acodec);
            actx->sample_fmt  = PickSampleFmt(acodec, AV_SAMPLE_FMT_FLTP);
            actx->sample_rate = (int)sampleRate;
            av_channel_layout_default(&actx->ch_layout, (int)channels);
            actx->bit_rate  = 192000;
            actx->time_base = (AVRational){ 1, (int)sampleRate };
            if (ofmt->oformat->flags & AVFMT_GLOBALHEADER) actx->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;

            if (ast && avcodec_open2(actx, acodec, NULL) == 0) {
                avcodec_parameters_from_context(ast->codecpar, actx);
                ast->time_base = actx->time_base;

                av_channel_layout_default(&inLayout, (int)channels);
                if (swr_alloc_set_opts2(&swr, &actx->ch_layout, actx->sample_fmt, actx->sample_rate,
                                        &inLayout, AV_SAMPLE_FMT_S16, (int)sampleRate, 0, NULL) >= 0 &&
                    swr && swr_init(swr) >= 0) {
                    aframe = av_frame_alloc();
                    aframe->format     = actx->sample_fmt;
                    av_channel_layout_copy(&aframe->ch_layout, &actx->ch_layout);
                    aframe->sample_rate = actx->sample_rate;
                    audioFrameSize = actx->frame_size > 0 ? actx->frame_size : 1024;
                    aframe->nb_samples = audioFrameSize;
                    if (av_frame_get_buffer(aframe, 0) < 0) { av_frame_free(&aframe); swr_free(&swr); }
                } else {
                    if (swr) swr_free(&swr);
                }
            } else {
                if (actx) avcodec_free_context(&actx);
                actx = NULL;
                ast  = NULL;
            }
        }
    }

    if (!(ofmt->oformat->flags & AVFMT_NOFILE)) {
        if (avio_open(&ofmt->pb, path, AVIO_FLAG_WRITE) < 0) {
            SetErr(outError, errorCap, "Could not open \"%s\" for writing", path);
            goto cleanup;
        }
    }
    if (avformat_write_header(ofmt, NULL) < 0) { SetErr(outError, errorCap, "Could not write mp4 header for \"%s\"", path); goto cleanup; }

    pkt = av_packet_alloc();
    if (!pkt) { SetErr(outError, errorCap, "Out of memory exporting \"%s\"", path); goto cleanup; }

    // ---- video ----
    {
        int64_t vpts = 0;
        uint32_t frameIdx = 0;
        bool haveFrame = true;
        while (haveFrame) {
            av_frame_make_writable(vframe);
            const uint8_t *srcSlices[1] = { rgba };
            int            srcStride[1] = { w * 4 };
            sws_scale(sws, srcSlices, srcStride, 0, h, vframe->data, vframe->linesize);
            vframe->pts = vpts++;

            avcodec_send_frame(vctx, vframe);
            while (avcodec_receive_packet(vctx, pkt) == 0) {
                pkt->stream_index = vst->index;
                av_packet_rescale_ts(pkt, vctx->time_base, vst->time_base);
                av_interleaved_write_frame(ofmt, pkt);
            }

            frameIdx++;
            if (progress) progress(progressUser, frameCount ? (float)frameIdx / (float)frameCount : -1.0f);
            haveFrame = PssVideoDecoder_NextFrame(videoDec, &rgba, &w, &h);
        }
        avcodec_send_frame(vctx, NULL);
        while (avcodec_receive_packet(vctx, pkt) == 0) {
            pkt->stream_index = vst->index;
            av_packet_rescale_ts(pkt, vctx->time_base, vst->time_base);
            av_interleaved_write_frame(ofmt, pkt);
        }
    }

    // ---- audio ----
    if (actx) {
        uint32_t pos = 0;
        int64_t  apts = 0;
        while (pos < pcmFrames) {
            uint32_t take = (pcmFrames - pos < (uint32_t)audioFrameSize) ? (pcmFrames - pos) : (uint32_t)audioFrameSize;
            const uint8_t *inData[1] = { (const uint8_t *)(pcm + (size_t)pos * channels) };
            int converted = swr_convert(swr, aframe->data, audioFrameSize, inData, (int)take);
            if (converted <= 0) break;
            aframe->nb_samples = converted;
            aframe->pts = apts;
            apts += converted;
            avcodec_send_frame(actx, aframe);
            while (avcodec_receive_packet(actx, pkt) == 0) {
                pkt->stream_index = ast->index;
                av_packet_rescale_ts(pkt, actx->time_base, ast->time_base);
                av_interleaved_write_frame(ofmt, pkt);
            }
            pos += take;
        }
        for (;;) {   // drain resampler
            int converted = swr_convert(swr, aframe->data, audioFrameSize, NULL, 0);
            if (converted <= 0) break;
            aframe->nb_samples = converted;
            aframe->pts = apts;
            apts += converted;
            avcodec_send_frame(actx, aframe);
            while (avcodec_receive_packet(actx, pkt) == 0) {
                pkt->stream_index = ast->index;
                av_packet_rescale_ts(pkt, actx->time_base, ast->time_base);
                av_interleaved_write_frame(ofmt, pkt);
            }
        }
        avcodec_send_frame(actx, NULL);
        while (avcodec_receive_packet(actx, pkt) == 0) {
            pkt->stream_index = ast->index;
            av_packet_rescale_ts(pkt, actx->time_base, ast->time_base);
            av_interleaved_write_frame(ofmt, pkt);
        }
    }

    av_write_trailer(ofmt);
    ok = true;

cleanup:
    av_channel_layout_uninit(&inLayout);
    av_dict_free(&voptions);
    if (pkt) av_packet_free(&pkt);
    if (vframe) av_frame_free(&vframe);
    if (aframe) av_frame_free(&aframe);
    if (sws) sws_freeContext(sws);
    if (swr) swr_free(&swr);
    if (vctx) avcodec_free_context(&vctx);
    if (actx) avcodec_free_context(&actx);
    if (ofmt) {
        if (!(ofmt->oformat->flags & AVFMT_NOFILE) && ofmt->pb) avio_closep(&ofmt->pb);
        avformat_free_context(ofmt);
    }
    PssVideoDecoder_Rewind(videoDec);
    return ok;
}
