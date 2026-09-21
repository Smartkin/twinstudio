#include <limits.h>
#include <math.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <clay.h>
#include <raylib.h>
#include <rpmalloc.h>
#include <tinyfiledialogs.h>

#include "memory/memory.h"
#include "ui/ui.h"

#include "mp4_io.h"
#include "pss_codec.h"
#include "pss_format.h"

#define TS_RENDERER_IMPLEMENTATION
#include "render/renderer.h"

// ---------------------------------------------------------------------------
// Minimal thread + mutex shim (same shape as src/music_manager/main.c, so a
// background job never blocks the render loop).
// ---------------------------------------------------------------------------

#if defined(_WIN32)
    #define WIN32_LEAN_AND_MEAN
    #define NOGDI
    #define NOUSER
    #include <windows.h>

    typedef HANDLE           TsThread;
    typedef CRITICAL_SECTION TsMutex;

    static void TsMutexInit(TsMutex *m)    { InitializeCriticalSection(m); }
    static void TsMutexFree(TsMutex *m)    { DeleteCriticalSection(m); }
    static void TsMutexLock(TsMutex *m)    { EnterCriticalSection(m); }
    static void TsMutexUnlock(TsMutex *m)  { LeaveCriticalSection(m); }

    typedef DWORD TsThreadRet;
    #define TS_THREAD_CALL WINAPI
    static bool TsThreadStart(TsThread *t, TsThreadRet (TS_THREAD_CALL *fn)(void *), void *arg) {
        *t = CreateThread(NULL, 0, fn, arg, 0, NULL);
        return *t != NULL;
    }
    static void TsThreadJoin(TsThread *t) {
        if (*t) { WaitForSingleObject(*t, INFINITE); CloseHandle(*t); *t = NULL; }
    }
    #define TS_THREAD_RETURN 0
#else
    #include <pthread.h>

    typedef pthread_t       TsThread;
    typedef pthread_mutex_t TsMutex;

    static void TsMutexInit(TsMutex *m)    { pthread_mutex_init(m, NULL); }
    static void TsMutexFree(TsMutex *m)    { pthread_mutex_destroy(m); }
    static void TsMutexLock(TsMutex *m)    { pthread_mutex_lock(m); }
    static void TsMutexUnlock(TsMutex *m)  { pthread_mutex_unlock(m); }

    typedef void *TsThreadRet;
    #define TS_THREAD_CALL
    static bool TsThreadStart(TsThread *t, TsThreadRet (*fn)(void *), void *arg) {
        return pthread_create(t, NULL, fn, arg) == 0;
    }
    static void TsThreadJoin(TsThread *t) { pthread_join(*t, NULL); }
    #define TS_THREAD_RETURN NULL
#endif

// ============================================================================
//  Clay + Raylib + ffmpeg PSS video player/editor
//
//  Layout
//    menu bar     : Open / Save / Import MP4 / Export MP4
//    video area   : the currently decoded frame, letterboxed
//    seek bar     : click/drag to jump to a frame
//    track row    : which audio (dub language) track is playing, if >1
//    transport    : Play/Pause, Advance (single frame step), Restart
//
//  A PSS file (see pss_format.h) is parsed into an MPEG2 elementary stream
//  plus zero or more SPU2-ADPCM/PCM audio tracks (one per dub language).
//  Video is decoded frame by frame on the main thread through libavcodec
//  (pss_codec.h) - MPEG2 at FMV resolutions decodes comfortably within a
//  frame budget, so there is no separate decode thread. Opening, saving,
//  importing and exporting all run on a single background worker, mirroring
//  src/music_manager/main.c's loader: the worker fills a staging area, the
//  main thread publishes it.
//
//  Seeking has no keyframe index: a backward seek rewinds the decoder and
//  decodes forward from the start, discarding frames before the target; a
//  forward seek just keeps decoding from wherever playback already is. That
//  is fine for FMV-length clips (seconds to a few minutes) but would be slow
//  for anything much longer.
// ============================================================================

// ---------------------------------------------------------------------------
// Theme
// ---------------------------------------------------------------------------

#define C_BG         (Clay_Color){ 17,  18,  23, 255}
#define C_PANEL      (Clay_Color){ 24,  26,  33, 255}
#define C_PANEL_2    (Clay_Color){ 31,  34,  43, 255}
#define C_HOVER      (Clay_Color){ 43,  47,  59, 255}
#define C_SELECT     (Clay_Color){ 38,  62,  97, 255}
#define C_ACCENT     (Clay_Color){ 96, 174, 255, 255}
#define C_TEXT       (Clay_Color){231, 235, 243, 255}
#define C_TEXT_DIM   (Clay_Color){145, 152, 168, 255}
#define C_TEXT_FAINT (Clay_Color){ 96, 103, 118, 255}
#define C_WAVE       (Clay_Color){ 62,  68,  84, 255}
#define C_LINE       (Clay_Color){ 44,  48,  60, 255}
#define C_DANGER     (Clay_Color){122,  46,  46, 255}
#define C_DANGER_HOT (Clay_Color){168,  62,  62, 255}
#define C_ERROR_TEXT (Clay_Color){235, 110, 110, 255}
#define C_ON_ACCENT  (Clay_Color){ 12,  18,  28, 255}

#define FONT_BODY  0
#define FONT_TITLE 0

#define APP_NAME    "Twinsanity PSS Video Editor"
#define APP_VERSION "1.0"
#define MENU_H      44.0f
#define TRACK_ROW_H 34.0f
#define SEEK_BAR_H  10.0f
#define TRANSPORT_H 68.0f
#define VOLUME_BAR_W 80.0f
// Fixed width for the transport bar's left (volume) and right (time/frame)
// zones - equal on both sides so the play/pause/etc buttons between them
// stay centered on the row regardless of the time text's own width, and
// wide enough that "99:59 / 99:59  (frame 99999/99999)" never wraps.
#define TRANSPORT_ZONE_W 230.0f
#define PROGRESS_W  360.0f

#define MAX_AUDIO_TRACKS 16

// ---------------------------------------------------------------------------
// Target platform limits. The missing-periodic-pack_header bug (see the
// PSS_BLOCK_SIZE comment in pss_format.c) was why a round-tripped video got
// silently skipped, but resolution turned out to matter too: importing at
// 1920x1080 crashed the game outright. Both the resolution cap and the frame
// rate cap are enforced again.
// ---------------------------------------------------------------------------

typedef enum { PSS_STD_NTSC = 0, PSS_STD_PAL } PssVideoStandard;

// The region (PAL/NTSC) only affects frame rate - 25fps for PAL, 30 for
// NTSC. Maximum resolution is NOT region-specific: 640x480 works for both
// (confirmed in-game), and it's the aspect ratio chosen (see PssTargetAspect
// in mp4_io.h), not the region, that decides how big a video can actually
// be - 640x480 exactly for 4:3, or 512x288 for 16:9 (640 isn't a multiple
// of the 16:9 quantization step, so it settles for the next size down; see
// Mp4ComputeImportTarget's comment).
static Mp4ImportLimits StandardLimits(PssVideoStandard std) {
    return (Mp4ImportLimits){ .maxWidth = 640, .maxHeight = 480, .maxFps = std == PSS_STD_PAL ? 25.0 : 30.0 };
}
static const char *StandardName(PssVideoStandard std) { return std == PSS_STD_PAL ? "PAL" : "NTSC"; }

static PssVideoStandard g_targetStandard = PSS_STD_NTSC;

// ---------------------------------------------------------------------------
// Per-frame string arena (Clay only stores pointers, so strings must outlive
// the layout pass - this is reset once per frame before Clay_BeginLayout).
// ---------------------------------------------------------------------------

static char g_strArena[64 * 1024];
static int  g_strUsed = 0;

static void ResetFrameStrings(void) { g_strUsed = 0; }

static Clay_String Fmt(const char *fmt, ...) {
    char *dst = g_strArena + g_strUsed;
    int avail = (int)sizeof(g_strArena) - g_strUsed;
    if (avail < 2) return (Clay_String){ .length = 0, .chars = "" };
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(dst, (size_t)avail, fmt, ap);
    va_end(ap);
    if (n < 0) n = 0;
    if (n >= avail) n = avail - 1;
    g_strUsed += n + 1;
    return (Clay_String){ .length = n, .chars = dst };
}

static Clay_String TimeStr(double seconds) {
    if (seconds < 0 || seconds != seconds) seconds = 0;
    int total = (int)seconds;
    return Fmt("%d:%02d", total / 60, total % 60);
}

static Clay_TextElementConfig TextCfg(uint16_t size, Clay_Color color, uint16_t fontId) {
    return (Clay_TextElementConfig){ .fontId = fontId, .fontSize = size, .textColor = color };
}
static Clay_TextElementConfig TextCfgNoWrap(uint16_t size, Clay_Color color, uint16_t fontId) {
    return (Clay_TextElementConfig){ .fontId = fontId, .fontSize = size, .textColor = color,
                                     .wrapMode = CLAY_TEXT_WRAP_NONE };
}

static bool PointInBox(Clay_BoundingBox b, Vector2 p) {
    return p.x >= b.x && p.x <= b.x + b.width && p.y >= b.y && p.y <= b.y + b.height;
}
static bool CtrlDown(void) {
    return IsKeyDown(KEY_LEFT_CONTROL) || IsKeyDown(KEY_RIGHT_CONTROL) ||
           IsKeyDown(KEY_LEFT_SUPER)   || IsKeyDown(KEY_RIGHT_SUPER);
}

// ---------------------------------------------------------------------------
// Document: whatever is currently loaded, from either an opened .pss or an
// imported video. Video is always kept as an MPEG2 elementary stream; each
// audio track (there can be more than one - one per dub language) is kept as
// decoded PCM, the representation both playback and re-encoding (back to
// its original PSS audio type for .pss, to AAC for .mp4) start from. `type`
// preserves whatever the track was opened as (PCM16 or ADPCM) so saving
// doesn't silently convert it - every retail sample seen so far is PCM16LE,
// so freshly imported audio defaults to that rather than ADPCM.
// ---------------------------------------------------------------------------

typedef struct {
    int16_t *pcm;
    uint32_t pcmFrames, sampleRate, channels;
    PssAudioType type;
} DecodedAudioTrack;

typedef struct {
    bool loaded;
    TwinStudio_Arena arena;   // owns everything below

    uint8_t *videoEs;
    uint32_t videoEsSize;
    int      width, height;
    double   fps;
    uint32_t totalFrames;     // from a one-time full decode pass at load time; 0 if unknown

    // The source file's own PTS pre-roll (see PssContainer::hasInitialPts),
    // carried from Open through to Save so a round-tripped retail file keeps
    // its original startup timing instead of restarting the timeline at 0 -
    // false/0 for anything this app built itself (a fresh MP4 import has no
    // equivalent value to carry forward).
    bool     hasInitialPts;
    uint64_t initialPtsTicks;

    DecodedAudioTrack audioTracks[MAX_AUDIO_TRACKS];
    uint32_t audioTrackCount;
    uint32_t activeTrack;      // index into audioTracks currently played

    char path[1024];          // last opened/saved .pss path; "" if never saved
    bool dirty;                // true if the in-memory document isn't on disk at `path`

    // true if this document came from Open (a real retail PSS with a
    // meaningful resolution to preserve), false if it came from Import (a
    // fresh MP4 encode with no inherent target resolution). ActiveImportLimits
    // only matches a currently-loaded document's resolution exactly when
    // this is true - otherwise importing a second MP4 on top of a first
    // (still-unsaved) import would wrongly treat the first import's
    // arbitrary resolution as a fixed target to match, showing "doesn't
    // match the open PSS's resolution" instead of the generic engine-limits
    // prompt.
    bool fromOpen;
} PssDocument;

static PssDocument g_doc = { 0 };

static const DecodedAudioTrack *ActiveAudioTrack(void) {
    if (g_doc.audioTrackCount == 0 || g_doc.activeTrack >= g_doc.audioTrackCount) return NULL;
    return &g_doc.audioTracks[g_doc.activeTrack];
}

// ---------------------------------------------------------------------------
// Background loader: Open / Import / Save / Export. One job runs at a time;
// see src/music_manager/main.c's ArchiveLoader for the pattern this mirrors.
// ---------------------------------------------------------------------------

typedef enum { JOB_NONE = 0, JOB_OPEN, JOB_IMPORT, JOB_SAVE, JOB_EXPORT } JobKind;
typedef enum { LOAD_IDLE = 0, LOAD_RUNNING, LOAD_DONE, LOAD_FAILED } LoadState;

typedef struct {
    TsMutex  mutex;
    TsThread thread;
    bool     threadValid;

    // --- guarded by mutex ---
    LoadState state;
    JobKind   job;
    float     progress;    // -1 = indeterminate, else 0..1
    char      status[96];
    char      error[256];
    char      path[1024];
    Mp4ImportLimits importLimits;   // set by StartImportJob() before the JOB_IMPORT worker starts

    // --- worker-owned until state leaves LOAD_RUNNING, then main-thread-owned
    //     until PumpLoader() resets it back to LOAD_IDLE. Not mutex-guarded:
    //     the state transition above is the memory barrier between the two. ---
    TwinStudio_Arena stagingArena;
    uint8_t *videoEs; uint32_t videoEsSize;
    int      width, height; double fps;
    uint32_t totalFrames;
    bool     hasInitialPts;
    uint64_t initialPtsTicks;
    DecodedAudioTrack audioTracks[MAX_AUDIO_TRACKS];
    uint32_t audioTrackCount;
} Loader;

static Loader g_loader = { .progress = -1.0f };

static LoadState LoaderState(void) {
    TsMutexLock(&g_loader.mutex);
    LoadState s = g_loader.state;
    TsMutexUnlock(&g_loader.mutex);
    return s;
}
static bool IsBusy(void) { return LoaderState() == LOAD_RUNNING; }

static void LoaderSetStatus(const char *text, float progress) {
    TsMutexLock(&g_loader.mutex);
    snprintf(g_loader.status, sizeof(g_loader.status), "%s", text);
    g_loader.progress = progress;
    TsMutexUnlock(&g_loader.mutex);
}
static void LoaderSetProgress(float progress) {
    TsMutexLock(&g_loader.mutex);
    g_loader.progress = progress;
    TsMutexUnlock(&g_loader.mutex);
}
static void LoaderFinish(LoadState state, const char *error) {
    TsMutexLock(&g_loader.mutex);
    g_loader.state = state;
    if (error) snprintf(g_loader.error, sizeof(g_loader.error), "%s", error);
    TsMutexUnlock(&g_loader.mutex);
}
static void ImportProgress(void *user, float frac) { (void)user; LoaderSetProgress(frac); }
static void ExportProgress(void *user, float frac) { (void)user; LoaderSetProgress(frac); }

// ---------------------------------------------------------------------------
// Playback (main thread only: it owns the GL context and the audio device)
// ---------------------------------------------------------------------------

typedef struct {
    PssVideoDecoder *dec;
    bool     hasTex;
    Texture2D tex;
    int      texW, texH;

    bool     hasStream;
    AudioStream stream;
    bool     streamStarted;
    uint32_t audioCursor;      // next PCM frame to push, into the active track

    bool     playing;
    bool     atEnd;
    double   fps;
    double   frameAccum;
    uint32_t frameIndex;
} PlayerState;

static PlayerState g_player = { 0 };

// ---------------------------------------------------------------------------
// UI state / actions
// ---------------------------------------------------------------------------

typedef enum { PENDING_NONE = 0, PENDING_OPEN, PENDING_IMPORT, PENDING_CLOSE, PENDING_EXIT } PendingAction;

typedef struct {
    bool confirmOpen;
    PendingAction pendingAction;

    // Modal, like confirmOpen/compressWarningOpen, but lowest-priority of
    // the three - either of the others outranks it if somehow both end up
    // requested at once.
    bool aboutOpen;

    bool     seekDragging;
    uint32_t seekPreviewFrame;

    bool     volumeDragging;
    float    volume;   // 0..1; applied to the current stream and any new one (see StartAudioStreamForActiveTrack)

    // Import compression warning: shown when a probed source exceeds the
    // current target standard's limits, before the (possibly slow) real
    // import starts. `pendingImportPath` doubles as "is one queued".
    bool     compressWarningOpen;
    char     pendingImportPath[1024];
    int      pendingImportSrcW, pendingImportSrcH;
    double   pendingImportSrcFps;
    int      pendingImportOutW, pendingImportOutH;
    double   pendingImportOutFps;
    bool     pendingImportExact;   // true = pendingImportOutW/H is an exact stretch-to-fill canvas, not just a cap
    PssTargetAspect pendingImportAspectChoice;   // which ratio pendingImportOutW/H reflects; meaningless if exact
} UIState;

static UIState g_ui = { .volume = 0.7f };
static bool    g_quit = false;

#define AUDIO_CHUNK_FRAMES 2048u
static int16_t g_audioPushBuf[AUDIO_CHUNK_FRAMES * 8];   // up to 8ch headroom, only channels*frames used

static void StopPlayback(void) {
    if (g_player.hasStream) { StopAudioStream(g_player.stream); UnloadAudioStream(g_player.stream); }
    if (g_player.hasTex) UnloadTexture(g_player.tex);
    if (g_player.dec) PssVideoDecoder_Destroy(g_player.dec);
    g_player = (PlayerState){ 0 };
}

static bool AdvanceOneFrame(void) {
    if (!g_player.dec) return false;
    const uint8_t *rgba; int w, h;
    if (!PssVideoDecoder_NextFrame(g_player.dec, &rgba, &w, &h)) {
        g_player.atEnd   = true;
        g_player.playing = false;
        if (g_player.hasStream) PauseAudioStream(g_player.stream);
        return false;
    }
    if (!g_player.hasTex || g_player.texW != w || g_player.texH != h) {
        if (g_player.hasTex) UnloadTexture(g_player.tex);
        Image img = { .data = (void *)rgba, .width = w, .height = h, .mipmaps = 1,
                     .format = PIXELFORMAT_UNCOMPRESSED_R8G8B8A8 };
        g_player.tex  = LoadTextureFromImage(img);
        g_player.hasTex = true;
        g_player.texW = w; g_player.texH = h;
    } else {
        UpdateTexture(g_player.tex, rgba);
    }
    g_player.frameIndex++;
    return true;
}

static void StartAudioStreamForActiveTrack(void) {
    const DecodedAudioTrack *t = ActiveAudioTrack();
    if (t && t->channels > 0 && t->pcm) {
        g_player.stream = LoadAudioStream(t->sampleRate, 16, (int)t->channels);
        g_player.hasStream = true;
        SetAudioStreamVolume(g_player.stream, g_ui.volume);
    }
}

static void StartPlayback(void) {
    g_player.dec = PssVideoDecoder_Create(g_doc.videoEs, g_doc.videoEsSize);
    if (!g_player.dec) return;

    AdvanceOneFrame();                          // show frame 0 immediately
    g_player.fps = PssVideoDecoder_FrameRate(g_player.dec);

    StartAudioStreamForActiveTrack();

    g_player.playing = true;
    if (g_player.hasStream) { PlayAudioStream(g_player.stream); g_player.streamStarted = true; }
}

static void PumpPlayerAudio(void) {
    if (!g_player.hasStream || !g_player.playing) return;
    const DecodedAudioTrack *t = ActiveAudioTrack();
    if (!t) return;
    uint32_t ch = t->channels;
    while (IsAudioStreamProcessed(g_player.stream)) {
        uint32_t avail = (t->pcmFrames > g_player.audioCursor) ? (t->pcmFrames - g_player.audioCursor) : 0;
        uint32_t take  = avail < AUDIO_CHUNK_FRAMES ? avail : AUDIO_CHUNK_FRAMES;
        if (take > 0) {
            memcpy(g_audioPushBuf, t->pcm + (size_t)g_player.audioCursor * ch,
                  (size_t)take * ch * sizeof(int16_t));
            g_player.audioCursor += take;
        }
        if (take < AUDIO_CHUNK_FRAMES) {
            memset(g_audioPushBuf + (size_t)take * ch, 0, (size_t)(AUDIO_CHUNK_FRAMES - take) * ch * sizeof(int16_t));
        }
        UpdateAudioStream(g_player.stream, g_audioPushBuf, (int)AUDIO_CHUNK_FRAMES);
    }
}

// Re-syncs the audio cursor to wherever the video currently is (used after a
// seek or a track switch) and restarts the stream from there.
static void ResyncAudioToVideo(bool resumeIfPlaying) {
    const DecodedAudioTrack *t = ActiveAudioTrack();
    if (!t || !g_player.hasStream) return;
    double time = g_player.fps > 0 ? (double)g_player.frameIndex / g_player.fps : 0.0;
    uint32_t sample = (uint32_t)(time * t->sampleRate);
    g_player.audioCursor = sample > t->pcmFrames ? t->pcmFrames : sample;
    StopAudioStream(g_player.stream);
    g_player.streamStarted = false;
    if (resumeIfPlaying && g_player.playing) { PlayAudioStream(g_player.stream); g_player.streamStarted = true; }
}

static void RestartPlayback(void) {
    if (!g_player.dec) return;
    PssVideoDecoder_Rewind(g_player.dec);
    g_player.frameAccum = 0.0;
    g_player.frameIndex = 0;
    g_player.atEnd = false;
    AdvanceOneFrame();
    ResyncAudioToVideo(true);
}

static void TogglePlayPause(void) {
    if (!g_player.dec) return;
    if (g_player.atEnd) {
        RestartPlayback();
        g_player.playing = true;
        if (g_player.hasStream) { PlayAudioStream(g_player.stream); g_player.streamStarted = true; }
        return;
    }
    g_player.playing = !g_player.playing;
    if (g_player.hasStream) {
        if (g_player.playing) {
            if (g_player.streamStarted) ResumeAudioStream(g_player.stream);
            else { PlayAudioStream(g_player.stream); g_player.streamStarted = true; }
        } else {
            PauseAudioStream(g_player.stream);
        }
    }
}

static void AdvanceFrame(void) {
    if (!g_player.dec) return;
    if (g_player.playing) {
        g_player.playing = false;
        if (g_player.hasStream) PauseAudioStream(g_player.stream);
    }
    AdvanceOneFrame();
    ResyncAudioToVideo(false);
}

// No keyframe index (see the file header comment): a backward seek rewinds
// and redecodes from frame 0, a forward seek just keeps decoding from
// wherever the decoder already is. Both discard every intermediate frame
// except the last, which is what actually gets displayed.
static void SeekToFrame(uint32_t targetFrame) {
    if (!g_player.dec) return;
    if (g_doc.totalFrames && targetFrame >= g_doc.totalFrames) targetFrame = g_doc.totalFrames - 1;

    if (targetFrame < g_player.frameIndex) {
        PssVideoDecoder_Rewind(g_player.dec);
        g_player.frameIndex = 0;
        g_player.atEnd = false;
    }
    while (g_player.frameIndex < targetFrame) {
        if (!AdvanceOneFrame()) break;
    }
    g_player.frameAccum = 0.0;
    ResyncAudioToVideo(true);
}

// Complementary to AdvanceFrame(): since decoding is forward-only, going
// back even one frame means the same rewind-and-redecode-from-zero SeekToFrame
// already does for any backward jump.
static void StepBackFrame(void) {
    if (!g_player.dec || g_player.frameIndex == 0) return;
    if (g_player.playing) {
        g_player.playing = false;
        if (g_player.hasStream) PauseAudioStream(g_player.stream);
    }
    SeekToFrame(g_player.frameIndex - 1);
}

static void SwitchAudioTrack(uint32_t index) {
    if (!g_doc.loaded || index >= g_doc.audioTrackCount || index == g_doc.activeTrack) return;
    if (g_player.hasStream) {
        StopAudioStream(g_player.stream);
        UnloadAudioStream(g_player.stream);
        g_player.hasStream = false;
        g_player.streamStarted = false;
    }
    g_doc.activeTrack = index;
    StartAudioStreamForActiveTrack();
    ResyncAudioToVideo(true);
}

static void UpdatePlayback(float dt) {
    if (!g_player.dec || IsBusy()) return;
    if (g_player.playing && !g_player.atEnd) {
        double frameDur = g_player.fps > 0 ? 1.0 / g_player.fps : 1.0 / 25.0;
        g_player.frameAccum += dt;
        int guard = 0;
        while (g_player.frameAccum >= frameDur && guard++ < 8) {
            g_player.frameAccum -= frameDur;
            if (!AdvanceOneFrame()) break;
        }
    }
    PumpPlayerAudio();
}

// ---------------------------------------------------------------------------
// Loading a whole file into memory (used for Open; Import goes through
// ffmpeg's own demuxer in mp4_io.c instead).
// ---------------------------------------------------------------------------

static uint8_t *ReadWholeFile(const char *path, size_t *outSize) {
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    if (size < 0) { fclose(f); return NULL; }
    fseek(f, 0, SEEK_SET);

    uint8_t *buf = (uint8_t *)malloc((size_t)size);
    if (!buf) { fclose(f); return NULL; }
    size_t got = fread(buf, 1, (size_t)size, f);
    fclose(f);
    if (got != (size_t)size) { free(buf); return NULL; }

    *outSize = (size_t)size;
    return buf;
}

// Decodes the whole video once just to count frames, so the seek bar has a
// real range to work with. Thrown away afterwards - playback uses its own
// decoder instance.
// Also reports the decoded picture's own width/height/fps - JOB_OPEN has no
// other way to learn these without this scan it already does for
// totalFrames (PssContainer_Parse only sees the elementary stream's raw
// bytes, never decodes them), and leaving g_doc.width/height at 0 for an
// opened-not-imported file broke Import's "match the open document exactly"
// path for anyone who hadn't already played/stepped the video at least once
// (the only other place these got set).
static uint32_t ScanFrameCount(const uint8_t *videoEs, uint32_t videoEsSize, int *outWidth, int *outHeight, double *outFps) {
    PssVideoDecoder *dec = PssVideoDecoder_Create(videoEs, videoEsSize);
    if (!dec) return 0;
    uint32_t count = 0;
    const uint8_t *rgba; int w, h;
    while (PssVideoDecoder_NextFrame(dec, &rgba, &w, &h)) {
        if (count == 0) { *outWidth = w; *outHeight = h; }
        count++;
    }
    *outFps = PssVideoDecoder_FrameRate(dec);
    PssVideoDecoder_Destroy(dec);
    return count;
}

// ---------------------------------------------------------------------------
// Background worker
// ---------------------------------------------------------------------------

static TsThreadRet TS_THREAD_CALL LoaderWorker(void *arg) {
    (void)arg;
    rpmalloc_thread_initialize();

    char path[1024];
    JobKind job;
    TsMutexLock(&g_loader.mutex);
    snprintf(path, sizeof(path), "%s", g_loader.path);
    job = g_loader.job;
    TsMutexUnlock(&g_loader.mutex);

    char err[256]; err[0] = '\0';
    bool ok = false;

    if (job == JOB_OPEN) {
        LoaderSetStatus("Reading file", -1);
        size_t fileSize = 0;
        uint8_t *bytes = ReadWholeFile(path, &fileSize);
        if (!bytes || fileSize == 0) {
            snprintf(err, sizeof(err), "Could not read \"%s\"", path);
        } else {
            // ADPCM decodes to roughly 3.5x its size; 6x plus a margin leaves
            // room for that, the raw video/audio copies, and bookkeeping.
            size_t arenaSize = fileSize * 6u + 4u * 1024u * 1024u;
            g_loader.stagingArena = TwinStudio_CreateArena(arenaSize);

            LoaderSetStatus("Parsing PSS container", -1);
            PssContainer container;
            if (!PssContainer_Parse(bytes, fileSize, &g_loader.stagingArena, &container, err, sizeof(err))) {
                TwinStudio_ArenaFree(&g_loader.stagingArena);
            } else if (!container.video.present) {
                snprintf(err, sizeof(err), "\"%s\" has no video stream", path);
                TwinStudio_ArenaFree(&g_loader.stagingArena);
            } else {
                g_loader.videoEs     = container.video.data;
                g_loader.videoEsSize = container.video.size;
                g_loader.width = g_loader.height = 0;   // known once the first frame is decoded
                g_loader.fps = 0;
                g_loader.hasInitialPts   = container.hasInitialPts;
                g_loader.initialPtsTicks = container.initialPtsTicks;

                uint32_t trackCount = container.audioTrackCount;
                if (trackCount > MAX_AUDIO_TRACKS) {
                    fprintf(stderr, "pss: %u audio tracks found, only decoding the first %d\n",
                           trackCount, MAX_AUDIO_TRACKS);
                    trackCount = MAX_AUDIO_TRACKS;
                }
                for (uint32_t i = 0; i < trackCount; i++) {
                    LoaderSetStatus(container.audioTrackCount > 1 ? "Decoding audio tracks" : "Decoding audio", -1);
                    DecodedAudioTrack *dst = &g_loader.audioTracks[g_loader.audioTrackCount];
                    if (PssAudioTrack_DecodeToPcm(&container.audioTracks[i], &g_loader.stagingArena,
                                                  &dst->pcm, &dst->pcmFrames)) {
                        dst->sampleRate = container.audioTracks[i].sampleRate;
                        dst->channels   = container.audioTracks[i].channels;
                        dst->type       = container.audioTracks[i].type;
                        g_loader.audioTrackCount++;
                    } else {
                        fprintf(stderr, "pss: could not decode audio track %u\n", i);
                    }
                }

                LoaderSetStatus("Scanning video length", -1);
                g_loader.totalFrames = ScanFrameCount(g_loader.videoEs, g_loader.videoEsSize,
                                                      &g_loader.width, &g_loader.height, &g_loader.fps);
                ok = true;
            }
            free(bytes);
        }
    } else if (job == JOB_IMPORT) {
        LoaderSetStatus("Importing video", -1);
        long srcSize = 0;
        FILE *probe = fopen(path, "rb");
        if (probe) { fseek(probe, 0, SEEK_END); srcSize = ftell(probe); fclose(probe); }

        size_t arenaSize = (size_t)(srcSize > 0 ? srcSize : 16 * 1024 * 1024) * 5u + 64u * 1024u * 1024u;
        g_loader.stagingArena = TwinStudio_CreateArena(arenaSize);

        Mp4ImportResult res;
        if (Mp4Import(path, &g_loader.stagingArena, &g_loader.importLimits, &res, ImportProgress, NULL, err, sizeof(err))) {
            g_loader.videoEs = res.videoEs; g_loader.videoEsSize = res.videoEsSize;
            g_loader.width = res.width; g_loader.height = res.height; g_loader.fps = res.fps;
            if (res.pcm && res.pcmFrames > 0) {
                g_loader.audioTracks[0] = (DecodedAudioTrack){
                    .pcm = res.pcm, .pcmFrames = res.pcmFrames,
                    .sampleRate = res.sampleRate, .channels = res.channels,
                    .type = PSS_AUDIO_PCM16_LE
                };
                g_loader.audioTrackCount = 1;
            }
            LoaderSetStatus("Scanning video length", -1);
            int scanW, scanH; double scanFps;   // already known from the import itself; discarded here
            g_loader.totalFrames = ScanFrameCount(g_loader.videoEs, g_loader.videoEsSize, &scanW, &scanH, &scanFps);
            ok = true;
        } else {
            TwinStudio_ArenaFree(&g_loader.stagingArena);
        }
    } else if (job == JOB_SAVE) {
        LoaderSetStatus("Encoding PSS", -1);

        size_t audioEstimate = 0;
        for (uint32_t i = 0; i < g_doc.audioTrackCount; i++) {
            const DecodedAudioTrack *t = &g_doc.audioTracks[i];
            if (t->type == PSS_AUDIO_VAG_ADPCM) {
                uint32_t blocksPerChannel = (t->pcmFrames + 27u) / 28u;
                audioEstimate += (size_t)((double)blocksPerChannel * 16.0 * t->channels * 1.2) + 4096u;
            } else {
                // PCM16: 2 bytes/sample, plus rounding up to whole 512-byte
                // interleave groups per channel.
                audioEstimate += (size_t)t->pcmFrames * t->channels * 2u + 4096u;
            }
        }
        size_t arenaSize = (size_t)g_doc.videoEsSize * 2u + audioEstimate * 2u + 1u * 1024u * 1024u;
        TwinStudio_Arena saveArena = TwinStudio_CreateArena(arenaSize);

        PssContainer container; memset(&container, 0, sizeof(container));
        container.video.present = true;
        container.video.data    = g_doc.videoEs;
        container.video.size    = g_doc.videoEsSize;
        container.video.fps     = g_doc.fps;
        container.hasInitialPts   = g_doc.hasInitialPts;
        container.initialPtsTicks = g_doc.initialPtsTicks;

        if (g_doc.audioTrackCount > 0) {
            container.audioTracks = (PssAudioTrack *)TwinStudio_ArenaAlloc(
                &saveArena, g_doc.audioTrackCount * sizeof(PssAudioTrack));
            container.audioTrackCount = g_doc.audioTrackCount;
            for (uint32_t i = 0; i < g_doc.audioTrackCount; i++) {
                const DecodedAudioTrack *src = &g_doc.audioTracks[i];
                if (!PssAudioTrack_EncodeFromPcm(&saveArena, src->pcm, src->pcmFrames,
                                                 src->sampleRate, src->channels, src->type,
                                                 &container.audioTracks[i])) {
                    snprintf(err, sizeof(err), "Could not encode audio track %u", i);
                    goto save_done;
                }
            }
        }

        {
            uint8_t *outData = NULL; uint32_t outSize = 0;
            if (!PssContainer_Write(&container, &saveArena, &outData, &outSize)) {
                snprintf(err, sizeof(err), "Could not encode the PSS container");
            } else {
                FILE *f = fopen(path, "wb");
                if (!f) {
                    snprintf(err, sizeof(err), "Could not write \"%s\"", path);
                } else {
                    size_t written = fwrite(outData, 1, outSize, f);
                    fclose(f);
                    ok = (written == outSize);
                    if (!ok) snprintf(err, sizeof(err), "Short write to \"%s\"", path);
                }
            }
        }
    save_done:
        TwinStudio_ArenaFree(&saveArena);
    } else if (job == JOB_EXPORT) {
        // Exports whichever track is currently selected for playback; mp4
        // multi-audio-track authoring is out of scope here.
        LoaderSetStatus("Exporting MP4", 0.0f);
        PssVideoDecoder *dec = PssVideoDecoder_Create(g_doc.videoEs, g_doc.videoEsSize);
        if (!dec) {
            snprintf(err, sizeof(err), "Could not open the video stream for export");
        } else {
            const DecodedAudioTrack *t = ActiveAudioTrack();
            ok = Mp4Export(path, dec, g_doc.totalFrames, t ? t->pcm : NULL, t ? t->pcmFrames : 0,
                           t ? t->sampleRate : 0, t ? t->channels : 0,
                           ExportProgress, NULL, err, sizeof(err));
            PssVideoDecoder_Destroy(dec);
        }
    }

    LoaderFinish(ok ? LOAD_DONE : LOAD_FAILED, err);
    rpmalloc_thread_finalize();
    return TS_THREAD_RETURN;
}

static void StartJob(JobKind job, const char *path) {
    if (IsBusy()) return;
    TsMutexLock(&g_loader.mutex);
    snprintf(g_loader.path, sizeof(g_loader.path), "%s", path ? path : "");
    g_loader.status[0] = '\0';
    g_loader.error[0]  = '\0';
    g_loader.progress  = -1.0f;
    g_loader.job   = job;
    g_loader.state = LOAD_RUNNING;
    g_loader.videoEs = NULL; g_loader.videoEsSize = 0;
    g_loader.width = g_loader.height = 0; g_loader.fps = 0; g_loader.totalFrames = 0;
    g_loader.hasInitialPts = false; g_loader.initialPtsTicks = 0;
    memset(g_loader.audioTracks, 0, sizeof(g_loader.audioTracks));
    g_loader.audioTrackCount = 0;
    TsMutexUnlock(&g_loader.mutex);

    if (!TsThreadStart(&g_loader.thread, LoaderWorker, NULL)) {
        g_loader.threadValid = false;
        LoaderFinish(LOAD_FAILED, "Could not start the worker thread");
        return;
    }
    g_loader.threadValid = true;
}

// Moves a finished Open/Import into the live document, replacing whatever
// was loaded before (torn down first, since it owns the audio device / GL
// texture the new one is about to claim).
static void PublishDocument(void) {
    StopPlayback();
    if (g_doc.loaded) TwinStudio_ArenaFree(&g_doc.arena);

    g_doc.arena       = g_loader.stagingArena;
    g_doc.videoEs     = g_loader.videoEs;
    g_doc.videoEsSize = g_loader.videoEsSize;
    g_doc.width       = g_loader.width;
    g_doc.height      = g_loader.height;
    g_doc.fps         = g_loader.fps;
    g_doc.totalFrames = g_loader.totalFrames;
    g_doc.hasInitialPts   = g_loader.hasInitialPts;
    g_doc.initialPtsTicks = g_loader.initialPtsTicks;
    memcpy(g_doc.audioTracks, g_loader.audioTracks, sizeof(g_doc.audioTracks));
    g_doc.audioTrackCount = g_loader.audioTrackCount;
    g_doc.activeTrack     = 0;
    g_doc.loaded      = true;

    g_doc.fromOpen = (g_loader.job == JOB_OPEN);
    if (g_doc.fromOpen) {
        snprintf(g_doc.path, sizeof(g_doc.path), "%s", g_loader.path);
        g_doc.dirty = false;
        // Match the PAL/NTSC toggle to whatever this file's own frame rate
        // implies (25fps PAL, 30fps NTSC - see StandardLimits), so a
        // subsequent Import defaults to the same region's frame rate cap
        // instead of whatever was left over from before.
        if (g_doc.fps > 0.0) {
            g_targetStandard = fabs(g_doc.fps - 30.0) < fabs(g_doc.fps - 25.0) ? PSS_STD_NTSC : PSS_STD_PAL;
        }
    } else {
        g_doc.path[0] = '\0';
        g_doc.dirty = true;      // imported content only exists in memory so far
    }

    memset(&g_loader.stagingArena, 0, sizeof(g_loader.stagingArena));
    g_loader.videoEs = NULL;
    memset(g_loader.audioTracks, 0, sizeof(g_loader.audioTracks));
    g_loader.audioTrackCount = 0;

    StartPlayback();
}

static char g_lastError[256] = { 0 };

static void PumpLoader(void) {
    LoadState state = LoaderState();
    if (state != LOAD_DONE && state != LOAD_FAILED) return;

    if (g_loader.threadValid) { TsThreadJoin(&g_loader.thread); g_loader.threadValid = false; }

    JobKind job = g_loader.job;

    if (state == LOAD_FAILED) {
        TsMutexLock(&g_loader.mutex);
        snprintf(g_lastError, sizeof(g_lastError), "%s", g_loader.error);
        TsMutexUnlock(&g_loader.mutex);
    } else {
        g_lastError[0] = '\0';
        if (job == JOB_OPEN || job == JOB_IMPORT) {
            PublishDocument();
        } else if (job == JOB_SAVE) {
            snprintf(g_doc.path, sizeof(g_doc.path), "%s", g_loader.path);
            g_doc.dirty = false;
        }
    }

    TsMutexLock(&g_loader.mutex);
    g_loader.state = LOAD_IDLE;
    g_loader.job   = JOB_NONE;
    TsMutexUnlock(&g_loader.mutex);
}

// ---------------------------------------------------------------------------
// UI actions
// ---------------------------------------------------------------------------

static void DoOpen(void) {
    static const char *patterns[] = { "*.pss", "*.PSS" };
    char *path = tinyfd_openFileDialog("Open PSS video", NULL, 2, patterns, "PSS video", 0);
    if (!path) return;
    StartJob(JOB_OPEN, path);
}

// Import defaults to matching whatever PSS is currently open, exactly -
// not just fitting within the platform's generic maximum - since the game
// does not dynamically resize the on-screen area it reserves for a given
// cutscene to fit an oddly-sized replacement; a smaller or differently-
// shaped video than the original just leaves the difference as untouched
// black space instead of filling the screen. Falls back to the generic
// platform standard when nothing's loaded to match against, or when the
// loaded document is itself a not-yet-saved import (see PssDocument::
// fromOpen) - its resolution isn't a real target to match, it's just
// whatever the previous import happened to land on, so importing a second
// MP4 on top of it should get the normal engine-limits prompt instead.
static Mp4ImportLimits ActiveImportLimits(void) {
    if (g_doc.loaded && g_doc.fromOpen && g_doc.width > 0 && g_doc.height > 0) {
        Mp4ImportLimits standard = StandardLimits(g_targetStandard);
        return (Mp4ImportLimits){
            .maxWidth = g_doc.width, .maxHeight = g_doc.height,
            .maxFps = g_doc.fps > 0.0 ? g_doc.fps : standard.maxFps,
            .exact = true,
        };
    }
    return StandardLimits(g_targetStandard);
}

static void StartImportJob(const char *path, PssTargetAspect aspect) {
    g_loader.importLimits = ActiveImportLimits();
    g_loader.importLimits.aspect = aspect;
    StartJob(JOB_IMPORT, path);
}

static void DoImport(void) {
    static const char *patterns[] = { "*.mp4", "*.MP4" };
    char *path = tinyfd_openFileDialog("Import video", NULL, 2, patterns, "MP4 video", 0);
    if (!path) return;

    int srcW = 0, srcH = 0; double srcFps = 0;
    char probeErr[256];
    if (Mp4ProbeVideo(path, &srcW, &srcH, &srcFps, probeErr, sizeof(probeErr))) {
        Mp4ImportLimits limits = ActiveImportLimits();
        int outW, outH; double outFps;
        Mp4ComputeImportTarget(srcW, srcH, srcFps, &limits, &outW, &outH, &outFps);

        if (outW != srcW || outH != srcH || outFps < srcFps - 0.01) {
            snprintf(g_ui.pendingImportPath, sizeof(g_ui.pendingImportPath), "%s", path);
            g_ui.pendingImportSrcW = srcW; g_ui.pendingImportSrcH = srcH; g_ui.pendingImportSrcFps = srcFps;
            g_ui.pendingImportOutW = outW; g_ui.pendingImportOutH = outH; g_ui.pendingImportOutFps = outFps;
            g_ui.pendingImportExact = limits.exact;
            // Default to whichever ratio AUTO picked, so the dialog opens
            // showing the same result as before - the user can switch to
            // the other one before confirming. Meaningless (left at AUTO)
            // for the exact-match case, since that canvas size is fixed.
            if (limits.exact) {
                g_ui.pendingImportAspectChoice = PSS_ASPECT_AUTO;
            } else {
                double srcAspect = (double)srcW / (double)srcH;
                g_ui.pendingImportAspectChoice = fabs(srcAspect - 16.0 / 9.0) < fabs(srcAspect - 4.0 / 3.0)
                                                     ? PSS_ASPECT_16_9 : PSS_ASPECT_4_3;
            }
            g_ui.compressWarningOpen = true;
            return;
        }
    }
    // Either already within limits, or the probe failed - let the real
    // import surface its own error in that case rather than blocking here.
    StartImportJob(path, PSS_ASPECT_AUTO);
}

static void DoSave(void) {
    if (!g_doc.loaded) return;
    static const char *patterns[] = { "*.pss" };
    char *path = tinyfd_saveFileDialog("Save PSS video", g_doc.path[0] ? g_doc.path : "video.pss",
                                       1, patterns, "PSS video");
    if (!path) return;
    StartJob(JOB_SAVE, path);
}
static void DoExport(void) {
    if (!g_doc.loaded) return;
    static const char *patterns[] = { "*.mp4" };
    char *path = tinyfd_saveFileDialog("Export to MP4", "video.mp4", 1, patterns, "MP4 video");
    if (!path) return;
    StartJob(JOB_EXPORT, path);
}

// Unloads the current document with nothing new to replace it - lets a
// chain of Imports go back to a clean slate (and the generic engine-limits
// prompt, per ActiveImportLimits' comment) without having to Open something
// else first just to clear the "match this resolution" target.
static void DoClose(void) {
    if (!g_doc.loaded) return;
    StopPlayback();
    TwinStudio_ArenaFree(&g_doc.arena);
    memset(&g_doc, 0, sizeof(g_doc));
}

static void RunPendingAction(PendingAction action) {
    switch (action) {
        case PENDING_OPEN:   DoOpen();      break;
        case PENDING_IMPORT: DoImport();    break;
        case PENDING_CLOSE:  DoClose();     break;
        case PENDING_EXIT:   g_quit = true; break;
        case PENDING_NONE:   break;
    }
}

// Opening, importing, or closing over an unsaved document loses it, so ask first.
static void RequestAction(PendingAction action) {
    if (IsBusy()) return;
    if (!g_doc.dirty) { RunPendingAction(action); return; }
    g_ui.confirmOpen   = true;
    g_ui.pendingAction = action;
}
static void OnOpenClicked(void)   { RequestAction(PENDING_OPEN); }
static void OnImportClicked(void) { RequestAction(PENDING_IMPORT); }
static void OnCloseClicked(void)  { RequestAction(PENDING_CLOSE); }

// ---------------------------------------------------------------------------
// Input
// ---------------------------------------------------------------------------

static void HandleConfirmDialog(Vector2 mouse) {
    if (!g_ui.confirmOpen) return;

    if (IsKeyPressed(KEY_ESCAPE)) { g_ui.confirmOpen = false; g_ui.pendingAction = PENDING_NONE; return; }
    if (!IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) return;

    PendingAction action = g_ui.pendingAction;

    if (Clay_PointerOver(Clay_GetElementId(CLAY_STRING("ConfirmDiscardButton")))) {
        g_ui.confirmOpen = false; g_ui.pendingAction = PENDING_NONE;
        RunPendingAction(action);
        return;
    }
    if (Clay_PointerOver(Clay_GetElementId(CLAY_STRING("ConfirmCancelButton")))) {
        g_ui.confirmOpen = false; g_ui.pendingAction = PENDING_NONE;
        return;
    }

    Clay_ElementData card = Clay_GetElementData(Clay_GetElementId(CLAY_STRING("ConfirmDialog")));
    if (card.found && !PointInBox(card.boundingBox, mouse)) {
        g_ui.confirmOpen = false; g_ui.pendingAction = PENDING_NONE;
    }
}

static void HandleCompressWarningDialog(Vector2 mouse) {
    if (!g_ui.compressWarningOpen) return;

    if (IsKeyPressed(KEY_ESCAPE)) { g_ui.compressWarningOpen = false; return; }
    if (!IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) return;

    // Aspect choice only applies to the generic (non-exact) case - an
    // exact-match canvas is already a fixed size, nothing to choose.
    if (!g_ui.pendingImportExact) {
        bool pick43  = Clay_PointerOver(Clay_GetElementId(CLAY_STRING("AspectChoice43")));
        bool pick169 = Clay_PointerOver(Clay_GetElementId(CLAY_STRING("AspectChoice169")));
        if (pick43 || pick169) {
            g_ui.pendingImportAspectChoice = pick43 ? PSS_ASPECT_4_3 : PSS_ASPECT_16_9;
            Mp4ImportLimits limits = StandardLimits(g_targetStandard);
            limits.aspect = g_ui.pendingImportAspectChoice;
            Mp4ComputeImportTarget(g_ui.pendingImportSrcW, g_ui.pendingImportSrcH, g_ui.pendingImportSrcFps,
                                   &limits, &g_ui.pendingImportOutW, &g_ui.pendingImportOutH, &g_ui.pendingImportOutFps);
            return;
        }
    }

    if (Clay_PointerOver(Clay_GetElementId(CLAY_STRING("CompressContinueButton")))) {
        g_ui.compressWarningOpen = false;
        StartImportJob(g_ui.pendingImportPath, g_ui.pendingImportAspectChoice);
        return;
    }
    if (Clay_PointerOver(Clay_GetElementId(CLAY_STRING("CompressCancelButton")))) {
        g_ui.compressWarningOpen = false;
        return;
    }

    Clay_ElementData card = Clay_GetElementData(Clay_GetElementId(CLAY_STRING("CompressDialog")));
    if (card.found && !PointInBox(card.boundingBox, mouse)) {
        g_ui.compressWarningOpen = false;
    }
}

static void HandleAboutDialog(Vector2 mouse) {
    if (!g_ui.aboutOpen) return;

    if (IsKeyPressed(KEY_ESCAPE)) { g_ui.aboutOpen = false; return; }
    if (!IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) return;

    if (Clay_PointerOver(Clay_GetElementId(CLAY_STRING("AboutCloseButton")))) {
        g_ui.aboutOpen = false;
        return;
    }

    Clay_ElementData card = Clay_GetElementData(Clay_GetElementId(CLAY_STRING("AboutDialog")));
    if (card.found && !PointInBox(card.boundingBox, mouse)) g_ui.aboutOpen = false;
}

static void HandleMenuBar(void) {
    if (!IsMouseButtonPressed(MOUSE_BUTTON_LEFT) || IsBusy()) return;
    if (Clay_PointerOver(Clay_GetElementId(CLAY_STRING("OpenButton")))) OnOpenClicked();
    else if (Clay_PointerOver(Clay_GetElementId(CLAY_STRING("SaveButton")))) { if (g_doc.loaded) DoSave(); }
    else if (Clay_PointerOver(Clay_GetElementId(CLAY_STRING("ImportButton")))) OnImportClicked();
    else if (Clay_PointerOver(Clay_GetElementId(CLAY_STRING("ExportButton")))) { if (g_doc.loaded) DoExport(); }
    else if (Clay_PointerOver(Clay_GetElementId(CLAY_STRING("CloseButton")))) { if (g_doc.loaded) OnCloseClicked(); }
    else if (Clay_PointerOver(Clay_GetElementId(CLAY_STRING("StandardToggle")))) {
        g_targetStandard = (g_targetStandard == PSS_STD_NTSC) ? PSS_STD_PAL : PSS_STD_NTSC;
    }
    else if (Clay_PointerOver(Clay_GetElementId(CLAY_STRING("AboutButton")))) g_ui.aboutOpen = true;
}

static void HandleTransportButtons(void) {
    if (!IsMouseButtonPressed(MOUSE_BUTTON_LEFT) || !g_doc.loaded || IsBusy()) return;
    if (Clay_PointerOver(Clay_GetElementId(CLAY_STRING("PlayButton")))) TogglePlayPause();
    else if (Clay_PointerOver(Clay_GetElementId(CLAY_STRING("StepBackButton")))) StepBackFrame();
    else if (Clay_PointerOver(Clay_GetElementId(CLAY_STRING("AdvanceButton")))) AdvanceFrame();
    else if (Clay_PointerOver(Clay_GetElementId(CLAY_STRING("RestartButton")))) RestartPlayback();
}

static void HandleAudioTrackSelector(void) {
    if (!IsMouseButtonPressed(MOUSE_BUTTON_LEFT) || !g_doc.loaded || IsBusy()) return;
    for (uint32_t i = 0; i < g_doc.audioTrackCount; i++) {
        if (Clay_PointerOver(CLAY_IDI("TrackButton", (int)i))) { SwitchAudioTrack(i); break; }
    }
}

static void HandleSeekBar(Vector2 mouse) {
    if (!g_doc.loaded || IsBusy() || g_doc.totalFrames == 0) return;

    Clay_ElementData bar = Clay_GetElementData(Clay_GetElementId(CLAY_STRING("SeekBar")));
    if (!bar.found) return;

    if (!g_ui.seekDragging && IsMouseButtonPressed(MOUSE_BUTTON_LEFT) && PointInBox(bar.boundingBox, mouse)) {
        g_ui.seekDragging = true;
    }
    if (g_ui.seekDragging) {
        float t = bar.boundingBox.width > 0 ? (mouse.x - bar.boundingBox.x) / bar.boundingBox.width : 0;
        t = Clamp(t, 0.0f, 1.0f);
        g_ui.seekPreviewFrame = (uint32_t)(t * (float)(g_doc.totalFrames - 1));

        if (IsMouseButtonReleased(MOUSE_BUTTON_LEFT)) {
            g_ui.seekDragging = false;
            SeekToFrame(g_ui.seekPreviewFrame);
        }
    }
}

static void HandleVolumeSlider(Vector2 mouse) {
    Clay_ElementData bar = Clay_GetElementData(Clay_GetElementId(CLAY_STRING("VolumeBar")));
    if (!bar.found) return;

    if (!g_ui.volumeDragging && IsMouseButtonPressed(MOUSE_BUTTON_LEFT) && PointInBox(bar.boundingBox, mouse)) {
        g_ui.volumeDragging = true;
    }
    if (g_ui.volumeDragging) {
        float v = bar.boundingBox.width > 0 ? (mouse.x - bar.boundingBox.x) / bar.boundingBox.width : 0;
        g_ui.volume = Clamp(v, 0.0f, 1.0f);
        if (g_player.hasStream) SetAudioStreamVolume(g_player.stream, g_ui.volume);

        if (IsMouseButtonReleased(MOUSE_BUTTON_LEFT)) g_ui.volumeDragging = false;
    }
}

static void HandleKeyboard(void) {
    if (IsBusy()) return;
    if (IsKeyPressed(KEY_SPACE)) TogglePlayPause();
    if (IsKeyPressed(KEY_RIGHT) || IsKeyPressed(KEY_PERIOD)) AdvanceFrame();
    if (IsKeyPressed(KEY_LEFT) || IsKeyPressed(KEY_COMMA)) StepBackFrame();
    if (IsKeyPressed(KEY_R)) RestartPlayback();
    if (CtrlDown() && IsKeyPressed(KEY_O)) OnOpenClicked();
    if (CtrlDown() && IsKeyPressed(KEY_S)) { if (g_doc.loaded) DoSave(); }
    if (CtrlDown() && IsKeyPressed(KEY_W)) { if (g_doc.loaded) OnCloseClicked(); }
}

// ---------------------------------------------------------------------------
// Icons
//
// Clay has no polygon primitive, so triangles (play/step chevrons) go
// through the CUSTOM_LAYOUT_ELEMENT_TYPE_TRIANGLE render command added to
// common/render/renderer.h. Like Clay's own strings, a CUSTOM element only
// stores a pointer to its data - it has to outlive Clay_EndLayout() until
// Clay_Raylib_Render() actually reads it, so triangle data is bump-allocated
// from a small per-frame arena the same way frame strings are.
// ---------------------------------------------------------------------------

#define MAX_FRAME_CUSTOM_ELEMENTS 64
static CustomLayoutElement g_frameCustomElements[MAX_FRAME_CUSTOM_ELEMENTS];
static int g_frameCustomElementsUsed = 0;

static void ResetFrameCustomElements(void) { g_frameCustomElementsUsed = 0; }

static void *PushTriangleElement(Clay_Color color, TriangleDirection dir) {
    if (g_frameCustomElementsUsed >= MAX_FRAME_CUSTOM_ELEMENTS) return NULL;
    CustomLayoutElement *e = &g_frameCustomElements[g_frameCustomElementsUsed++];
    e->type = CUSTOM_LAYOUT_ELEMENT_TYPE_TRIANGLE;
    e->customData.triangle.color = CLAY_COLOR_TO_RAYLIB_COLOR(color);
    e->customData.triangle.direction = dir;
    return e;
}

// `index` only needs to differ when a caller draws more than one triangle
// under the same parent in one layout (IconRestart's double chevron) -
// CLAY_ID_LOCAL alone hashes just the label and the parent's id, so two
// calls with the same label under the same parent would otherwise collide.
static void IconTriangle(int index, float w, float h, Clay_Color color, TriangleDirection dir) {
    CLAY(CLAY_IDI_LOCAL("Tri", index), {
        .layout = { .sizing = { .width = CLAY_SIZING_FIXED(w), .height = CLAY_SIZING_FIXED(h) } },
        .custom = { .customData = PushTriangleElement(color, dir) }
    }) {}
}

// Folder: a small tab sitting on top of the body. (Open)
static void IconFolder(Clay_Color color) {
    CLAY(CLAY_ID_LOCAL("IconFolder"), {
        .layout = {
            .layoutDirection = CLAY_TOP_TO_BOTTOM,
            .sizing = { .width = CLAY_SIZING_FIXED(16), .height = CLAY_SIZING_FIXED(14) }
        }
    }) {
        CLAY(CLAY_ID_LOCAL("Tab"), {
            .layout = { .sizing = { .width = CLAY_SIZING_FIXED(7), .height = CLAY_SIZING_FIXED(3) } },
            .backgroundColor = color
        }) {}
        CLAY(CLAY_ID_LOCAL("Body"), {
            .layout = { .sizing = { .width = CLAY_SIZING_FIXED(16), .height = CLAY_SIZING_FIXED(11) } },
            .backgroundColor = color,
            .cornerRadius = CLAY_CORNER_RADIUS(2)
        }) {}
    }
}

// Floppy disk: shutter and label cut out of the body. (Save)
static void IconDisk(Clay_Color color, Clay_Color cutout) {
    CLAY(CLAY_ID_LOCAL("IconDisk"), {
        .layout = {
            .layoutDirection = CLAY_TOP_TO_BOTTOM,
            .sizing = { .width = CLAY_SIZING_FIXED(15), .height = CLAY_SIZING_FIXED(15) },
            .padding = { .top = 2 },
            .childGap = 2,
            .childAlignment = { .x = CLAY_ALIGN_X_CENTER }
        },
        .backgroundColor = color,
        .cornerRadius = CLAY_CORNER_RADIUS(2)
    }) {
        CLAY(CLAY_ID_LOCAL("Shutter"), {
            .layout = { .sizing = { .width = CLAY_SIZING_FIXED(6), .height = CLAY_SIZING_FIXED(4) } },
            .backgroundColor = cutout
        }) {}
        CLAY(CLAY_ID_LOCAL("Label"), {
            .layout = { .sizing = { .width = CLAY_SIZING_FIXED(10), .height = CLAY_SIZING_FIXED(5) } },
            .backgroundColor = cutout
        }) {}
    }
}

// Arrow into a tray at the bottom. (Import)
static void IconImport(Clay_Color color) {
    CLAY(CLAY_ID_LOCAL("IconImport"), {
        .layout = {
            .layoutDirection = CLAY_TOP_TO_BOTTOM,
            .sizing = { .width = CLAY_SIZING_FIXED(14), .height = CLAY_SIZING_FIXED(16) },
            .childGap = 1,
            .childAlignment = { .x = CLAY_ALIGN_X_CENTER }
        }
    }) {
        CLAY(CLAY_ID_LOCAL("Stem"), {
            .layout = { .sizing = { .width = CLAY_SIZING_FIXED(4), .height = CLAY_SIZING_FIXED(6) } },
            .backgroundColor = color
        }) {}
        IconTriangle(0, 12, 7, color, TRIANGLE_DIR_DOWN);
        CLAY(CLAY_ID_LOCAL("Base"), {
            .layout = { .sizing = { .width = CLAY_SIZING_FIXED(14), .height = CLAY_SIZING_FIXED(2) } },
            .backgroundColor = color
        }) {}
    }
}

// Arrow out of a tray at the bottom. (Export)
static void IconExport(Clay_Color color) {
    CLAY(CLAY_ID_LOCAL("IconExport"), {
        .layout = {
            .layoutDirection = CLAY_TOP_TO_BOTTOM,
            .sizing = { .width = CLAY_SIZING_FIXED(14), .height = CLAY_SIZING_FIXED(16) },
            .childGap = 1,
            .childAlignment = { .x = CLAY_ALIGN_X_CENTER }
        }
    }) {
        IconTriangle(0, 12, 7, color, TRIANGLE_DIR_UP);
        CLAY(CLAY_ID_LOCAL("Stem"), {
            .layout = { .sizing = { .width = CLAY_SIZING_FIXED(4), .height = CLAY_SIZING_FIXED(6) } },
            .backgroundColor = color
        }) {}
        CLAY(CLAY_ID_LOCAL("Base"), {
            .layout = { .sizing = { .width = CLAY_SIZING_FIXED(14), .height = CLAY_SIZING_FIXED(2) } },
            .backgroundColor = color
        }) {}
    }
}

static void IconPlay(Clay_Color color) { IconTriangle(0, 18, 18, color, TRIANGLE_DIR_RIGHT); }

static void IconPause(Clay_Color color) {
    CLAY(CLAY_ID_LOCAL("IconPause"), {
        .layout = { .sizing = { .width = CLAY_SIZING_FIXED(18), .height = CLAY_SIZING_FIXED(18) },
                    .childGap = 4, .childAlignment = { .y = CLAY_ALIGN_Y_CENTER } }
    }) {
        CLAY(CLAY_ID_LOCAL("Bar1"), {
            .layout = { .sizing = { .width = CLAY_SIZING_FIXED(5), .height = CLAY_SIZING_FIXED(18) } },
            .backgroundColor = color, .cornerRadius = CLAY_CORNER_RADIUS(1)
        }) {}
        CLAY(CLAY_ID_LOCAL("Bar2"), {
            .layout = { .sizing = { .width = CLAY_SIZING_FIXED(5), .height = CLAY_SIZING_FIXED(18) } },
            .backgroundColor = color, .cornerRadius = CLAY_CORNER_RADIUS(1)
        }) {}
    }
}

// Triangle then a wall: one step forward.
static void IconStepForward(Clay_Color color) {
    CLAY(CLAY_ID_LOCAL("IconStepFwd"), {
        .layout = { .sizing = { .width = CLAY_SIZING_FIXED(16), .height = CLAY_SIZING_FIXED(15) },
                    .childGap = 2, .childAlignment = { .y = CLAY_ALIGN_Y_CENTER } }
    }) {
        IconTriangle(0, 10, 15, color, TRIANGLE_DIR_RIGHT);
        CLAY(CLAY_ID_LOCAL("Wall"), {
            .layout = { .sizing = { .width = CLAY_SIZING_FIXED(3), .height = CLAY_SIZING_FIXED(15) } },
            .backgroundColor = color
        }) {}
    }
}

// A wall then a triangle: one step back - the mirror of IconStepForward.
static void IconStepBack(Clay_Color color) {
    CLAY(CLAY_ID_LOCAL("IconStepBack"), {
        .layout = { .sizing = { .width = CLAY_SIZING_FIXED(16), .height = CLAY_SIZING_FIXED(15) },
                    .childGap = 2, .childAlignment = { .y = CLAY_ALIGN_Y_CENTER } }
    }) {
        CLAY(CLAY_ID_LOCAL("Wall"), {
            .layout = { .sizing = { .width = CLAY_SIZING_FIXED(3), .height = CLAY_SIZING_FIXED(15) } },
            .backgroundColor = color
        }) {}
        IconTriangle(0, 10, 15, color, TRIANGLE_DIR_LEFT);
    }
}

// A wall then a double chevron: rewind to the start, visually distinct from
// the single-chevron step-back.
static void IconRestart(Clay_Color color) {
    CLAY(CLAY_ID_LOCAL("IconRestart"), {
        .layout = { .sizing = { .width = CLAY_SIZING_FIXED(18), .height = CLAY_SIZING_FIXED(15) },
                    .childAlignment = { .y = CLAY_ALIGN_Y_CENTER } }
    }) {
        CLAY(CLAY_ID_LOCAL("Wall"), {
            .layout = { .sizing = { .width = CLAY_SIZING_FIXED(2), .height = CLAY_SIZING_FIXED(15) } },
            .backgroundColor = color
        }) {}
        IconTriangle(0, 8, 15, color, TRIANGLE_DIR_LEFT);
        IconTriangle(1, 8, 15, color, TRIANGLE_DIR_LEFT);
    }
}

// Speaker, matching src/music_manager/main.c's IconSpeaker: a small body
// then a cone stepped out of three slices (a single triangle here read as
// an arrow rather than a speaker), plus arcs that appear as volume rises.
static void IconVolume(Clay_Color color, int arcs) {
    CLAY(CLAY_ID_LOCAL("IconVolume"), {
        .layout = {
            .sizing = { .width = CLAY_SIZING_FIXED(20), .height = CLAY_SIZING_FIXED(14) },
            .childGap = 1,
            .childAlignment = { .y = CLAY_ALIGN_Y_CENTER }
        }
    }) {
        CLAY(CLAY_ID_LOCAL("Body"), {
            .layout = { .sizing = { .width = CLAY_SIZING_FIXED(3), .height = CLAY_SIZING_FIXED(6) } },
            .backgroundColor = color
        }) {}
        CLAY(CLAY_ID_LOCAL("Cone1"), {
            .layout = { .sizing = { .width = CLAY_SIZING_FIXED(2), .height = CLAY_SIZING_FIXED(8) } },
            .backgroundColor = color
        }) {}
        CLAY(CLAY_ID_LOCAL("Cone2"), {
            .layout = { .sizing = { .width = CLAY_SIZING_FIXED(2), .height = CLAY_SIZING_FIXED(11) } },
            .backgroundColor = color
        }) {}
        CLAY(CLAY_ID_LOCAL("Cone3"), {
            .layout = { .sizing = { .width = CLAY_SIZING_FIXED(2), .height = CLAY_SIZING_FIXED(14) } },
            .backgroundColor = color
        }) {}

        // Spacer keeps the arcs off the cone whether or not they are drawn.
        CLAY(CLAY_ID_LOCAL("Gap"), {
            .layout = { .sizing = { .width = CLAY_SIZING_FIXED(2), .height = CLAY_SIZING_FIXED(1) } }
        }) {}

        if (arcs >= 1) {
            CLAY(CLAY_ID_LOCAL("Arc1"), {
                .layout = { .sizing = { .width = CLAY_SIZING_FIXED(2), .height = CLAY_SIZING_FIXED(6) } },
                .backgroundColor = color,
                .cornerRadius = CLAY_CORNER_RADIUS(1)
            }) {}
        }
        if (arcs >= 2) {
            CLAY(CLAY_ID_LOCAL("Arc2"), {
                .layout = { .sizing = { .width = CLAY_SIZING_FIXED(2), .height = CLAY_SIZING_FIXED(11) } },
                .backgroundColor = color,
                .cornerRadius = CLAY_CORNER_RADIUS(1)
            }) {}
        }
    }
}

// Lower case "i" in a ring, matching src/music_manager/main.c's IconInfo.
static void IconInfo(Clay_Color color) {
    CLAY(CLAY_ID_LOCAL("IconInfo"), {
        .layout = {
            .layoutDirection = CLAY_TOP_TO_BOTTOM,
            .sizing = { .width = CLAY_SIZING_FIXED(14), .height = CLAY_SIZING_FIXED(14) },
            .padding = { .top = 3 },
            .childGap = 1,
            .childAlignment = { .x = CLAY_ALIGN_X_CENTER }
        },
        .cornerRadius = CLAY_CORNER_RADIUS(7),
        .border = { .width = CLAY_BORDER_OUTSIDE(2), .color = color }
    }) {
        CLAY(CLAY_ID_LOCAL("Dot"), {
            .layout = { .sizing = { .width = CLAY_SIZING_FIXED(2), .height = CLAY_SIZING_FIXED(2) } },
            .backgroundColor = color
        }) {}
        CLAY(CLAY_ID_LOCAL("Stem"), {
            .layout = { .sizing = { .width = CLAY_SIZING_FIXED(2), .height = CLAY_SIZING_FIXED(4) } },
            .backgroundColor = color
        }) {}
    }
}

// Eject glyph (triangle over a bar) - reads as "release the loaded media",
// which is exactly what Close does.
static void IconEject(Clay_Color color) {
    CLAY(CLAY_ID_LOCAL("IconEject"), {
        .layout = {
            .layoutDirection = CLAY_TOP_TO_BOTTOM,
            .sizing = { .width = CLAY_SIZING_FIXED(16), .height = CLAY_SIZING_FIXED(14) },
            .childGap = 3,
            .childAlignment = { .x = CLAY_ALIGN_X_CENTER }
        }
    }) {
        IconTriangle(0, 16, 9, color, TRIANGLE_DIR_UP);
        CLAY(CLAY_ID_LOCAL("Bar"), {
            .layout = { .sizing = { .width = CLAY_SIZING_FIXED(16), .height = CLAY_SIZING_FIXED(3) } },
            .backgroundColor = color
        }) {}
    }
}

// ---------------------------------------------------------------------------
// Rendering
// ---------------------------------------------------------------------------

typedef enum { MENU_ICON_NONE, MENU_ICON_OPEN, MENU_ICON_SAVE, MENU_ICON_IMPORT, MENU_ICON_EXPORT,
              MENU_ICON_EJECT, MENU_ICON_INFO } MenuIcon;
typedef enum { TRANSPORT_ICON_PLAY, TRANSPORT_ICON_PAUSE, TRANSPORT_ICON_STEP_BACK,
              TRANSPORT_ICON_STEP_FORWARD, TRANSPORT_ICON_RESTART } TransportIcon;

static void RenderMenuButton(Clay_ElementId id, Clay_String label, MenuIcon icon, bool enabled) {
    bool hovered = enabled && Clay_PointerOver(id);
    Clay_Color bg = !enabled ? C_PANEL : (hovered ? C_HOVER : C_PANEL_2);
    Clay_Color fg = !enabled ? C_TEXT_FAINT : (hovered ? C_TEXT : C_TEXT_DIM);
    Clay_Color mark = !enabled ? C_TEXT_FAINT : (hovered ? C_ACCENT : C_TEXT_DIM);

    CLAY(id, {
        .layout = {
            .sizing = { .height = CLAY_SIZING_FIXED(30) },
            .padding = { .left = 12, .right = 14 },
            .childGap = 8,
            .childAlignment = { .x = CLAY_ALIGN_X_CENTER, .y = CLAY_ALIGN_Y_CENTER }
        },
        .backgroundColor = bg,
        .cornerRadius = CLAY_CORNER_RADIUS(6),
        .border = { .width = CLAY_BORDER_OUTSIDE(1), .color = hovered ? C_ACCENT : C_LINE }
    }) {
        switch (icon) {
            case MENU_ICON_OPEN:   IconFolder(mark);       break;
            case MENU_ICON_SAVE:   IconDisk(mark, bg);     break;
            case MENU_ICON_IMPORT: IconImport(mark);       break;
            case MENU_ICON_EXPORT: IconExport(mark);       break;
            case MENU_ICON_EJECT:  IconEject(mark);        break;
            case MENU_ICON_INFO:   IconInfo(mark);         break;
            case MENU_ICON_NONE:                           break;
        }
        CLAY_TEXT(label, CLAY_TEXT_CONFIG(TextCfgNoWrap(14, fg, FONT_BODY)));
    }
}

static void RenderStandardToggle(void) {
    bool hovered = Clay_PointerOver(Clay_GetElementId(CLAY_STRING("StandardToggle")));
    CLAY(CLAY_ID("StandardToggle"), {
        .layout = {
            .sizing = { .height = CLAY_SIZING_FIXED(30) },
            .padding = { .left = 12, .right = 12 },
            .childAlignment = { .x = CLAY_ALIGN_X_CENTER, .y = CLAY_ALIGN_Y_CENTER }
        },
        .backgroundColor = hovered ? C_HOVER : C_PANEL_2,
        .cornerRadius = CLAY_CORNER_RADIUS(6),
        .border = { .width = CLAY_BORDER_OUTSIDE(1), .color = hovered ? C_ACCENT : C_LINE }
    }) {
        CLAY_TEXT(Fmt("%s target", StandardName(g_targetStandard)),
                  CLAY_TEXT_CONFIG(TextCfgNoWrap(13, C_TEXT_DIM, FONT_BODY)));
    }
}

static void RenderMenuBar(void) {
    CLAY(CLAY_ID("MenuBar"), {
        .layout = {
            .sizing = { .width = CLAY_SIZING_GROW(0), .height = CLAY_SIZING_FIXED(MENU_H) },
            .padding = { .left = 12, .right = 12 },
            .childGap = 8,
            .childAlignment = { .y = CLAY_ALIGN_Y_CENTER }
        },
        .backgroundColor = C_PANEL_2,
        .border = { .width = { .bottom = 1 }, .color = C_LINE }
    }) {
        RenderMenuButton(CLAY_ID("OpenButton"), CLAY_STRING("Open PSS"), MENU_ICON_OPEN, true);
        RenderMenuButton(CLAY_ID("SaveButton"), CLAY_STRING("Save PSS"), MENU_ICON_SAVE, g_doc.loaded);
        RenderMenuButton(CLAY_ID("ImportButton"), CLAY_STRING("Import MP4"), MENU_ICON_IMPORT, true);
        RenderMenuButton(CLAY_ID("ExportButton"), CLAY_STRING("Export MP4"), MENU_ICON_EXPORT, g_doc.loaded);
        RenderMenuButton(CLAY_ID("CloseButton"), CLAY_STRING("Close"), MENU_ICON_EJECT, g_doc.loaded);

        CLAY(CLAY_ID("MenuSpacer"), { .layout = { .sizing = { .width = CLAY_SIZING_GROW(0) } } }) {}

        if (g_lastError[0]) {
            CLAY_TEXT(Fmt("%s", g_lastError), CLAY_TEXT_CONFIG(TextCfgNoWrap(13, C_ERROR_TEXT, FONT_BODY)));
        } else if (g_doc.loaded) {
            CLAY_TEXT(Fmt("%dx%d  -  %.2f fps%s%s", g_doc.width ? g_doc.width : g_player.texW,
                         g_doc.height ? g_doc.height : g_player.texH,
                         g_player.fps, g_doc.audioTrackCount ? "  -  audio" : "",
                         g_doc.dirty ? "  -  unsaved" : ""),
                      CLAY_TEXT_CONFIG(TextCfgNoWrap(13, C_TEXT_FAINT, FONT_BODY)));
        }

        RenderStandardToggle();
        RenderMenuButton(CLAY_ID("AboutButton"), CLAY_STRING("About"), MENU_ICON_INFO, !IsBusy());
    }
}

static void RenderVideoArea(void) {
    CLAY(CLAY_ID("VideoArea"), {
        .layout = {
            .sizing = { .width = CLAY_SIZING_GROW(0), .height = CLAY_SIZING_GROW(0) },
            .padding = { .left = 24, .right = 24, .top = 24, .bottom = 24 },
            .childAlignment = { .x = CLAY_ALIGN_X_CENTER, .y = CLAY_ALIGN_Y_CENTER }
        },
        .backgroundColor = C_BG
    }) {
        if (g_player.hasTex) {
            // Clay's own aspectRatio + CLAY_SIZING_GROW(0) on both axes
            // doesn't reliably re-center the element when the aspect-fit
            // shrinks it below the full available width (it renders
            // anchored to its pre-shrink GROW width, leaving unaccounted
            // empty space on one side instead of splitting it evenly) - so
            // the fit is computed explicitly here instead, sized off
            // VideoArea's own last-frame measured box (one frame of lag on
            // a live window resize, imperceptible) rather than the broken
            // combination.
            Clay_ElementData area = Clay_GetElementData(Clay_GetElementId(CLAY_STRING("VideoArea")));
            float availW = area.found ? (area.boundingBox.width  - 48.0f) : 640.0f;
            float availH = area.found ? (area.boundingBox.height - 48.0f) : 480.0f;
            if (availW < 1.0f) availW = 1.0f;
            if (availH < 1.0f) availH = 1.0f;
            float texAspect = (float)g_player.texW / (float)g_player.texH;
            float w = availW, h = availW / texAspect;
            if (h > availH) { h = availH; w = availH * texAspect; }

            CLAY(CLAY_ID("VideoFrame"), {
                .layout = { .sizing = { .width = CLAY_SIZING_FIXED(w), .height = CLAY_SIZING_FIXED(h) } },
                .image = { .imageData = &g_player.tex }
            }) {}
        } else if (!IsBusy()) {
            CLAY_TEXT(CLAY_STRING("Open a .pss file or import an .mp4 to begin"),
                      CLAY_TEXT_CONFIG(TextCfgNoWrap(16, C_TEXT_DIM, FONT_BODY)));
        }
    }
}

static void RenderBusyOverlay(void) {
    if (!IsBusy()) return;

    TsMutexLock(&g_loader.mutex);
    float progress = g_loader.progress;
    char status[sizeof(g_loader.status)];
    snprintf(status, sizeof(status), "%s", g_loader.status);
    JobKind job = g_loader.job;
    TsMutexUnlock(&g_loader.mutex);

    bool determinate = progress >= 0.0f;
    float t = determinate ? Clamp(progress, 0.0f, 1.0f) : 0.0f;
    float segW = determinate ? t * PROGRESS_W : PROGRESS_W * 0.3f;
    float offset = 0.0f;
    if (!determinate) {
        float phase = (float)fmod(GetTime(), 1.6) / 1.6f;
        float tri = phase < 0.5f ? phase * 2.0f : (1.0f - phase) * 2.0f;
        offset = tri * (PROGRESS_W - segW);
    }

    static const char *jobLabel[] = { "", "Opening", "Importing", "Saving", "Exporting" };

    CLAY(CLAY_ID("BusyOverlay"), {
        .layout = {
            .layoutDirection = CLAY_TOP_TO_BOTTOM,
            .sizing = { .width = CLAY_SIZING_GROW(0), .height = CLAY_SIZING_GROW(0) },
            .childGap = 14,
            .childAlignment = { .x = CLAY_ALIGN_X_CENTER, .y = CLAY_ALIGN_Y_CENTER }
        },
        .backgroundColor = C_BG
    }) {
        CLAY_TEXT(Fmt("%s%s", jobLabel[job], determinate ? Fmt(" (%.0f%%)", t * 100.0f).chars : ""),
                  CLAY_TEXT_CONFIG(TextCfgNoWrap(16, C_TEXT, FONT_TITLE)));

        CLAY(CLAY_ID("ProgressTrack"), {
            .layout = {
                .sizing = { .width = CLAY_SIZING_FIXED(PROGRESS_W), .height = CLAY_SIZING_FIXED(6) },
                .padding = { .left = (uint16_t)offset },
                .childAlignment = { .y = CLAY_ALIGN_Y_CENTER }
            },
            .backgroundColor = C_WAVE,
            .cornerRadius = CLAY_CORNER_RADIUS(3)
        }) {
            CLAY(CLAY_ID("ProgressFill"), {
                .layout = { .sizing = { .width = CLAY_SIZING_FIXED(segW), .height = CLAY_SIZING_FIXED(6) } },
                .backgroundColor = C_ACCENT,
                .cornerRadius = CLAY_CORNER_RADIUS(3)
            }) {}
        }

        if (status[0]) CLAY_TEXT(Fmt("%s", status), CLAY_TEXT_CONFIG(TextCfgNoWrap(13, C_TEXT_DIM, FONT_BODY)));
    }
}

// Just the bar itself - no adjacent variable-width content (the time/frame
// counter used to sit in this row and made the bar visibly resize as its
// digit count changed; it now lives in the transport bar below, in its own
// fixed-width zone that doesn't affect anything else's layout).
static void RenderSeekBar(void) {
    bool loaded = g_doc.loaded && !IsBusy() && g_doc.totalFrames > 0;
    uint32_t shownFrame = g_ui.seekDragging ? g_ui.seekPreviewFrame : g_player.frameIndex;
    float frac = loaded ? Clamp((float)shownFrame / (float)(g_doc.totalFrames - 1 ? g_doc.totalFrames - 1 : 1), 0.0f, 1.0f) : 0.0f;

    CLAY(CLAY_ID("SeekBarRow"), {
        .layout = {
            .sizing = { .width = CLAY_SIZING_GROW(0), .height = CLAY_SIZING_FIXED(30) },
            .padding = { .left = 16, .right = 16 },
            .childAlignment = { .y = CLAY_ALIGN_Y_CENTER }
        },
        .backgroundColor = C_PANEL
    }) {
        CLAY(CLAY_ID("SeekBar"), {
            .layout = {
                .sizing = { .width = CLAY_SIZING_GROW(0), .height = CLAY_SIZING_FIXED(SEEK_BAR_H) },
            },
            .backgroundColor = C_WAVE,
            .cornerRadius = CLAY_CORNER_RADIUS(SEEK_BAR_H / 2.0f)
        }) {
            CLAY(CLAY_ID("SeekBarFill"), {
                .layout = { .sizing = { .width = CLAY_SIZING_PERCENT(frac), .height = CLAY_SIZING_FIXED(SEEK_BAR_H) } },
                .backgroundColor = loaded ? C_ACCENT : C_LINE,
                .cornerRadius = CLAY_CORNER_RADIUS(SEEK_BAR_H / 2.0f)
            }) {}
        }
    }
}

static void RenderAudioTrackSelector(void) {
    if (!g_doc.loaded || g_doc.audioTrackCount <= 1) return;

    CLAY(CLAY_ID("TrackRow"), {
        .layout = {
            .sizing = { .width = CLAY_SIZING_GROW(0), .height = CLAY_SIZING_FIXED(TRACK_ROW_H) },
            .padding = { .left = 16, .right = 16 },
            .childGap = 8,
            .childAlignment = { .y = CLAY_ALIGN_Y_CENTER }
        },
        .backgroundColor = C_PANEL_2,
        .border = { .width = { .top = 1 }, .color = C_LINE }
    }) {
        CLAY_TEXT(CLAY_STRING("Audio track:"), CLAY_TEXT_CONFIG(TextCfgNoWrap(13, C_TEXT_DIM, FONT_BODY)));
        for (uint32_t i = 0; i < g_doc.audioTrackCount; i++) {
            bool active = (i == g_doc.activeTrack);
            Clay_ElementId id = CLAY_IDI("TrackButton", (int)i);
            bool hovered = Clay_PointerOver(id);
            CLAY(id, {
                .layout = {
                    .sizing = { .height = CLAY_SIZING_FIXED(24) },
                    .padding = { .left = 10, .right = 10 },
                    .childAlignment = { .x = CLAY_ALIGN_X_CENTER, .y = CLAY_ALIGN_Y_CENTER }
                },
                .backgroundColor = active ? C_ACCENT : (hovered ? C_HOVER : C_PANEL_2),
                .cornerRadius = CLAY_CORNER_RADIUS(5),
                .border = { .width = CLAY_BORDER_OUTSIDE(1), .color = active ? C_ACCENT : C_LINE }
            }) {
                CLAY_TEXT(Fmt("Track %u", i + 1),
                          CLAY_TEXT_CONFIG(TextCfgNoWrap(12, active ? C_ON_ACCENT : C_TEXT_DIM, FONT_BODY)));
            }
        }
    }
}

static void RenderTransportButton(Clay_ElementId id, TransportIcon icon, bool primary, bool enabled) {
    bool hovered = enabled && Clay_PointerOver(id);
    float size = primary ? 56.0f : 42.0f;
    Clay_Color bg = !enabled ? C_PANEL_2
                  : primary  ? (hovered ? (Clay_Color){120, 190, 255, 255} : C_ACCENT)
                             : (hovered ? C_HOVER : C_PANEL_2);
    Clay_Color fg = !enabled ? C_TEXT_FAINT
                  : primary  ? C_ON_ACCENT
                             : C_TEXT;

    CLAY(id, {
        .layout = {
            .sizing = { .width = CLAY_SIZING_FIXED(size), .height = CLAY_SIZING_FIXED(size) },
            .childAlignment = { .x = CLAY_ALIGN_X_CENTER, .y = CLAY_ALIGN_Y_CENTER }
        },
        .backgroundColor = bg,
        .cornerRadius = CLAY_CORNER_RADIUS(size / 2.0f)
    }) {
        switch (icon) {
            case TRANSPORT_ICON_PLAY:          IconPlay(fg);         break;
            case TRANSPORT_ICON_PAUSE:         IconPause(fg);        break;
            case TRANSPORT_ICON_STEP_BACK:     IconStepBack(fg);     break;
            case TRANSPORT_ICON_STEP_FORWARD:  IconStepForward(fg);  break;
            case TRANSPORT_ICON_RESTART:       IconRestart(fg);      break;
        }
    }
}

static void RenderTransportBar(void) {
    bool loaded = g_doc.loaded && !IsBusy();
    uint32_t shownFrame = g_ui.seekDragging ? g_ui.seekPreviewFrame : g_player.frameIndex;
    bool showTime = g_doc.loaded && !IsBusy() && g_doc.totalFrames > 0;

    // Three zones so the transport buttons stay centered on the row
    // regardless of what's in the side zones: volume (bottom left), the
    // play/pause/etc buttons (center), and the time/frame counter (bottom
    // right) - both side zones are the same fixed width so the center zone
    // (which grows to fill whatever's left) is centered on the whole row.
    CLAY(CLAY_ID("TransportBar"), {
        .layout = {
            .sizing = { .width = CLAY_SIZING_GROW(0), .height = CLAY_SIZING_FIXED(TRANSPORT_H) },
            .padding = { .left = 16, .right = 16 },
            .childAlignment = { .y = CLAY_ALIGN_Y_CENTER }
        },
        .backgroundColor = C_PANEL,
        .border = { .width = { .top = 1 }, .color = C_LINE }
    }) {
        CLAY(CLAY_ID("TransportLeftZone"), {
            .layout = {
                .sizing = { .width = CLAY_SIZING_FIXED(TRANSPORT_ZONE_W) },
                .childGap = 8,
                .childAlignment = { .x = CLAY_ALIGN_X_LEFT, .y = CLAY_ALIGN_Y_CENTER }
            }
        }) {
            IconVolume(C_TEXT_DIM, g_ui.volume <= 0.001f ? 0 : (g_ui.volume < 0.5f ? 1 : 2));
            CLAY(CLAY_ID("VolumeBar"), {
                .layout = { .sizing = { .width = CLAY_SIZING_FIXED(VOLUME_BAR_W), .height = CLAY_SIZING_FIXED(SEEK_BAR_H) } },
                .backgroundColor = C_WAVE,
                .cornerRadius = CLAY_CORNER_RADIUS(SEEK_BAR_H / 2.0f)
            }) {
                CLAY(CLAY_ID("VolumeBarFill"), {
                    .layout = { .sizing = { .width = CLAY_SIZING_PERCENT(g_ui.volume), .height = CLAY_SIZING_FIXED(SEEK_BAR_H) } },
                    .backgroundColor = C_ACCENT,
                    .cornerRadius = CLAY_CORNER_RADIUS(SEEK_BAR_H / 2.0f)
                }) {}
            }
        }

        CLAY(CLAY_ID("TransportCenterZone"), {
            .layout = {
                .sizing = { .width = CLAY_SIZING_GROW(0), .height = CLAY_SIZING_GROW(0) },
                .childGap = 14,
                .childAlignment = { .x = CLAY_ALIGN_X_CENTER, .y = CLAY_ALIGN_Y_CENTER }
            }
        }) {
            RenderTransportButton(CLAY_ID("RestartButton"), TRANSPORT_ICON_RESTART, false, loaded);
            RenderTransportButton(CLAY_ID("StepBackButton"), TRANSPORT_ICON_STEP_BACK, false, loaded);
            RenderTransportButton(CLAY_ID("PlayButton"),
                                  (loaded && g_player.playing) ? TRANSPORT_ICON_PAUSE : TRANSPORT_ICON_PLAY,
                                  true, loaded);
            RenderTransportButton(CLAY_ID("AdvanceButton"), TRANSPORT_ICON_STEP_FORWARD, false, loaded);
        }

        CLAY(CLAY_ID("TransportRightZone"), {
            .layout = {
                .sizing = { .width = CLAY_SIZING_FIXED(TRANSPORT_ZONE_W), .height = CLAY_SIZING_GROW(0) },
                .childAlignment = { .x = CLAY_ALIGN_X_RIGHT, .y = CLAY_ALIGN_Y_CENTER }
            }
        }) {
            if (showTime) {
                double t = g_player.fps > 0 ? (double)shownFrame / g_player.fps : 0.0;
                double total = g_player.fps > 0 ? (double)g_doc.totalFrames / g_player.fps : 0.0;
                CLAY_TEXT(Fmt("%s / %s  (frame %u/%u)", TimeStr(t).chars, TimeStr(total).chars,
                             shownFrame, g_doc.totalFrames),
                          CLAY_TEXT_CONFIG(TextCfgNoWrap(13, C_TEXT_DIM, FONT_BODY)));
            }
        }
    }
}

typedef enum { CONFIRM_BTN_DANGER, CONFIRM_BTN_PRIMARY, CONFIRM_BTN_QUIET } ConfirmButtonStyle;

static void RenderConfirmButton(Clay_ElementId id, Clay_String label, ConfirmButtonStyle style) {
    bool hovered = Clay_PointerOver(id);
    Clay_Color bg, fg;
    if (style == CONFIRM_BTN_DANGER) {
        bg = hovered ? C_DANGER_HOT : C_DANGER;
        fg = (Clay_Color){250, 228, 228, 255};
    } else if (style == CONFIRM_BTN_PRIMARY) {
        bg = hovered ? (Clay_Color){120, 190, 255, 255} : C_ACCENT;
        fg = C_ON_ACCENT;
    } else {
        bg = hovered ? C_HOVER : C_PANEL_2;
        fg = hovered ? C_TEXT : C_TEXT_DIM;
    }

    CLAY(id, {
        .layout = {
            .sizing = { .width = CLAY_SIZING_FIXED(110), .height = CLAY_SIZING_FIXED(34) },
            .childAlignment = { .x = CLAY_ALIGN_X_CENTER, .y = CLAY_ALIGN_Y_CENTER }
        },
        .backgroundColor = bg,
        .cornerRadius = CLAY_CORNER_RADIUS(6),
        .border = { .width = CLAY_BORDER_OUTSIDE(1), .color = style == CONFIRM_BTN_QUIET ? C_LINE : bg }
    }) {
        CLAY_TEXT(label, CLAY_TEXT_CONFIG(TextCfgNoWrap(16, fg, FONT_BODY)));
    }
}

// A selectable option in the compress-warning dialog's "which ratio to
// stretch to" choice, styled like the audio track selector's active/
// inactive pattern.
static void RenderAspectChoiceButton(Clay_ElementId id, Clay_String label, int w, int h, bool selected) {
    bool hovered = Clay_PointerOver(id);
    Clay_Color bg = selected ? C_ACCENT : (hovered ? C_HOVER : C_PANEL_2);
    Clay_Color fg = selected ? C_ON_ACCENT : C_TEXT;
    Clay_Color fgDim = selected ? C_ON_ACCENT : C_TEXT_DIM;

    CLAY(id, {
        .layout = {
            .layoutDirection = CLAY_TOP_TO_BOTTOM,
            .sizing = { .width = CLAY_SIZING_GROW(0), .height = CLAY_SIZING_FIXED(48) },
            .padding = { .top = 6, .bottom = 6 },
            .childGap = 2,
            .childAlignment = { .x = CLAY_ALIGN_X_CENTER, .y = CLAY_ALIGN_Y_CENTER }
        },
        .backgroundColor = bg,
        .cornerRadius = CLAY_CORNER_RADIUS(6),
        .border = { .width = CLAY_BORDER_OUTSIDE(1), .color = selected ? C_ACCENT : C_LINE }
    }) {
        CLAY_TEXT(label, CLAY_TEXT_CONFIG(TextCfgNoWrap(14, fg, FONT_BODY)));
        CLAY_TEXT(Fmt("%dx%d", w, h), CLAY_TEXT_CONFIG(TextCfgNoWrap(12, fgDim, FONT_BODY)));
    }
}

static void RenderConfirmDialog(void) {
    if (!g_ui.confirmOpen) return;
    bool exiting = (g_ui.pendingAction == PENDING_EXIT);

    CLAY(CLAY_ID("ConfirmScrim"), {
        .layout = {
            .sizing = { .width = CLAY_SIZING_FIXED((float)GetScreenWidth()),
                        .height = CLAY_SIZING_FIXED((float)GetScreenHeight()) },
            .childAlignment = { .x = CLAY_ALIGN_X_CENTER, .y = CLAY_ALIGN_Y_CENTER }
        },
        .floating = {
            .zIndex = 110,
            .attachPoints = { .element = CLAY_ATTACH_POINT_LEFT_TOP, .parent = CLAY_ATTACH_POINT_LEFT_TOP },
            .pointerCaptureMode = CLAY_POINTER_CAPTURE_MODE_CAPTURE,
            .attachTo = CLAY_ATTACH_TO_ROOT
        },
        .backgroundColor = (Clay_Color){0, 0, 0, 180}
    }) {
        CLAY(CLAY_ID("ConfirmDialog"), {
            .layout = {
                .layoutDirection = CLAY_TOP_TO_BOTTOM,
                .sizing = { .width = CLAY_SIZING_FIXED(440) },
                .padding = { .left = 28, .right = 28, .top = 24, .bottom = 22 },
                .childGap = 6
            },
            .backgroundColor = C_PANEL,
            .cornerRadius = CLAY_CORNER_RADIUS(12),
            .border = { .width = CLAY_BORDER_OUTSIDE(1), .color = C_LINE }
        }) {
            CLAY_TEXT(exiting ? CLAY_STRING("Quit without saving?") : CLAY_STRING("Discard the current video?"),
                      CLAY_TEXT_CONFIG(TextCfg(20, C_TEXT, FONT_TITLE)));

            CLAY_AUTO_ID({ .layout = { .sizing = { .width = CLAY_SIZING_GROW(0), .height = CLAY_SIZING_FIXED(10) } } }) {}

            CLAY_TEXT(CLAY_STRING("The video currently loaded was imported and has not been saved as a .pss file yet."),
                      CLAY_TEXT_CONFIG(TextCfg(15, C_TEXT_DIM, FONT_BODY)));

            CLAY_AUTO_ID({
                .layout = { .sizing = { .width = CLAY_SIZING_GROW(0), .height = CLAY_SIZING_FIXED(18) } },
                .border = { .width = { .bottom = 1 }, .color = C_LINE }
            }) {}

            CLAY_AUTO_ID({
                .layout = { .sizing = { .width = CLAY_SIZING_GROW(0) }, .padding = { .top = 16 },
                            .childGap = 8, .childAlignment = { .y = CLAY_ALIGN_Y_CENTER } }
            }) {
                RenderConfirmButton(CLAY_ID("ConfirmCancelButton"), CLAY_STRING("Cancel"), CONFIRM_BTN_QUIET);
                CLAY_AUTO_ID({ .layout = { .sizing = { .width = CLAY_SIZING_GROW(0), .height = CLAY_SIZING_FIXED(1) } } }) {}
                RenderConfirmButton(CLAY_ID("ConfirmDiscardButton"),
                                    exiting ? CLAY_STRING("Quit anyway") : CLAY_STRING("Discard"), CONFIRM_BTN_DANGER);
            }
        }
    }
}

static void RenderCompressWarningDialog(void) {
    if (!g_ui.compressWarningOpen) return;

    CLAY(CLAY_ID("CompressScrim"), {
        .layout = {
            .sizing = { .width = CLAY_SIZING_FIXED((float)GetScreenWidth()),
                        .height = CLAY_SIZING_FIXED((float)GetScreenHeight()) },
            .childAlignment = { .x = CLAY_ALIGN_X_CENTER, .y = CLAY_ALIGN_Y_CENTER }
        },
        .floating = {
            .zIndex = 110,
            .attachPoints = { .element = CLAY_ATTACH_POINT_LEFT_TOP, .parent = CLAY_ATTACH_POINT_LEFT_TOP },
            .pointerCaptureMode = CLAY_POINTER_CAPTURE_MODE_CAPTURE,
            .attachTo = CLAY_ATTACH_TO_ROOT
        },
        .backgroundColor = (Clay_Color){0, 0, 0, 180}
    }) {
        CLAY(CLAY_ID("CompressDialog"), {
            .layout = {
                .layoutDirection = CLAY_TOP_TO_BOTTOM,
                .sizing = { .width = CLAY_SIZING_FIXED(460) },
                .padding = { .left = 28, .right = 28, .top = 24, .bottom = 22 },
                .childGap = 6
            },
            .backgroundColor = C_PANEL,
            .cornerRadius = CLAY_CORNER_RADIUS(12),
            .border = { .width = CLAY_BORDER_OUTSIDE(1), .color = C_LINE }
        }) {
            CLAY_TEXT(g_ui.pendingImportExact
                         ? CLAY_STRING("This video doesn't match the open PSS's resolution")
                         : CLAY_STRING("This video exceeds the engine's limits"),
                      CLAY_TEXT_CONFIG(TextCfg(20, C_TEXT, FONT_TITLE)));

            CLAY_AUTO_ID({ .layout = { .sizing = { .width = CLAY_SIZING_GROW(0), .height = CLAY_SIZING_FIXED(10) } } }) {}

            if (g_ui.pendingImportExact) {
                CLAY_TEXT(Fmt("The source is %dx%d @ %.2f fps; the currently open video is %dx%d. The game "
                             "doesn't resize a cutscene's on-screen area to fit a replacement of a different "
                             "size, so it needs to match exactly.",
                             g_ui.pendingImportSrcW, g_ui.pendingImportSrcH, g_ui.pendingImportSrcFps,
                             g_ui.pendingImportOutW, g_ui.pendingImportOutH),
                          CLAY_TEXT_CONFIG(TextCfg(15, C_TEXT_DIM, FONT_BODY)));

                CLAY_AUTO_ID({ .layout = { .sizing = { .width = CLAY_SIZING_GROW(0), .height = CLAY_SIZING_FIXED(6) } } }) {}

                CLAY_TEXT(Fmt("It will be stretched to fill exactly %dx%d%s before importing.",
                             g_ui.pendingImportOutW, g_ui.pendingImportOutH,
                             g_ui.pendingImportOutFps < g_ui.pendingImportSrcFps - 0.01
                                 ? Fmt(" and capped to %.0f fps", g_ui.pendingImportOutFps).chars : ""),
                          CLAY_TEXT_CONFIG(TextCfg(15, C_TEXT_DIM, FONT_BODY)));
            } else {
                Mp4ImportLimits limits = StandardLimits(g_targetStandard);
                CLAY_TEXT(Fmt("The source is %dx%d @ %.2f fps. %s PSS videos above %dx%d @ %.0f fps can crash or "
                             "fail to play in the game.",
                             g_ui.pendingImportSrcW, g_ui.pendingImportSrcH, g_ui.pendingImportSrcFps,
                             StandardName(g_targetStandard), limits.maxWidth, limits.maxHeight, limits.maxFps),
                          CLAY_TEXT_CONFIG(TextCfg(15, C_TEXT_DIM, FONT_BODY)));

                CLAY_AUTO_ID({ .layout = { .sizing = { .width = CLAY_SIZING_GROW(0), .height = CLAY_SIZING_FIXED(14) } } }) {}

                CLAY_TEXT(CLAY_STRING("Stretch to fill, at the maximum size for:"),
                          CLAY_TEXT_CONFIG(TextCfg(13, C_TEXT_DIM, FONT_BODY)));

                CLAY_AUTO_ID({ .layout = { .sizing = { .width = CLAY_SIZING_GROW(0), .height = CLAY_SIZING_FIXED(6) } } }) {}

                {
                    Mp4ImportLimits l43 = limits, l169 = limits;
                    l43.aspect  = PSS_ASPECT_4_3;
                    l169.aspect = PSS_ASPECT_16_9;
                    int w43, h43, w169, h169; double f43, f169;
                    Mp4ComputeImportTarget(g_ui.pendingImportSrcW, g_ui.pendingImportSrcH, g_ui.pendingImportSrcFps,
                                           &l43, &w43, &h43, &f43);
                    Mp4ComputeImportTarget(g_ui.pendingImportSrcW, g_ui.pendingImportSrcH, g_ui.pendingImportSrcFps,
                                           &l169, &w169, &h169, &f169);

                    CLAY_AUTO_ID({
                        .layout = { .sizing = { .width = CLAY_SIZING_GROW(0) }, .childGap = 8 }
                    }) {
                        RenderAspectChoiceButton(CLAY_ID("AspectChoice43"), CLAY_STRING("4:3"), w43, h43,
                                                 g_ui.pendingImportAspectChoice == PSS_ASPECT_4_3);
                        RenderAspectChoiceButton(CLAY_ID("AspectChoice169"), CLAY_STRING("16:9"), w169, h169,
                                                 g_ui.pendingImportAspectChoice == PSS_ASPECT_16_9);
                    }
                }

                CLAY_AUTO_ID({ .layout = { .sizing = { .width = CLAY_SIZING_GROW(0), .height = CLAY_SIZING_FIXED(14) } } }) {}

                CLAY_TEXT(Fmt("It will be stretched to fill exactly %dx%d%s before importing.",
                             g_ui.pendingImportOutW, g_ui.pendingImportOutH,
                             g_ui.pendingImportOutFps < g_ui.pendingImportSrcFps - 0.01
                                 ? Fmt(" and capped to %.0f fps", g_ui.pendingImportOutFps).chars : ""),
                          CLAY_TEXT_CONFIG(TextCfg(15, C_TEXT_DIM, FONT_BODY)));
            }

            CLAY_AUTO_ID({
                .layout = { .sizing = { .width = CLAY_SIZING_GROW(0), .height = CLAY_SIZING_FIXED(18) } },
                .border = { .width = { .bottom = 1 }, .color = C_LINE }
            }) {}

            CLAY_AUTO_ID({
                .layout = { .sizing = { .width = CLAY_SIZING_GROW(0) }, .padding = { .top = 16 },
                            .childGap = 8, .childAlignment = { .y = CLAY_ALIGN_Y_CENTER } }
            }) {
                RenderConfirmButton(CLAY_ID("CompressCancelButton"), CLAY_STRING("Cancel"), CONFIRM_BTN_QUIET);
                CLAY_AUTO_ID({ .layout = { .sizing = { .width = CLAY_SIZING_GROW(0), .height = CLAY_SIZING_FIXED(1) } } }) {}
                RenderConfirmButton(CLAY_ID("CompressContinueButton"), CLAY_STRING("Stretch & import"), CONFIRM_BTN_PRIMARY);
            }
        }
    }
}

// ---------------------------------------------------------------------------
// About dialog
// ---------------------------------------------------------------------------

static void RenderAboutRow(Clay_String key, Clay_String value) {
    CLAY_AUTO_ID({
        .layout = { .sizing = { .width = CLAY_SIZING_GROW(0) }, .childGap = 14,
                    .padding = { .top = 5, .bottom = 5 } }
    }) {
        CLAY_AUTO_ID({ .layout = { .sizing = { .width = CLAY_SIZING_FIXED(128) } } }) {
            CLAY_TEXT(key, CLAY_TEXT_CONFIG(TextCfgNoWrap(16, C_ACCENT, FONT_BODY)));
        }
        CLAY_TEXT(value, CLAY_TEXT_CONFIG(TextCfg(16, C_TEXT_DIM, FONT_BODY)));
    }
}

static void RenderAboutDialog(void) {
    if (!g_ui.aboutOpen) return;

    CLAY(CLAY_ID("AboutScrim"), {
        .layout = {
            .sizing = { .width = CLAY_SIZING_FIXED((float)GetScreenWidth()),
                        .height = CLAY_SIZING_FIXED((float)GetScreenHeight()) },
            .childAlignment = { .x = CLAY_ALIGN_X_CENTER, .y = CLAY_ALIGN_Y_CENTER }
        },
        .floating = {
            .zIndex = 100,
            .attachPoints = { .element = CLAY_ATTACH_POINT_LEFT_TOP, .parent = CLAY_ATTACH_POINT_LEFT_TOP },
            .pointerCaptureMode = CLAY_POINTER_CAPTURE_MODE_CAPTURE,
            .attachTo = CLAY_ATTACH_TO_ROOT
        },
        .backgroundColor = (Clay_Color){0, 0, 0, 170}
    }) {
        CLAY(CLAY_ID("AboutDialog"), {
            .layout = {
                .layoutDirection = CLAY_TOP_TO_BOTTOM,
                .sizing = { .width = CLAY_SIZING_FIXED(520) },
                .padding = { .left = 28, .right = 28, .top = 24, .bottom = 22 },
                .childGap = 6
            },
            .backgroundColor = C_PANEL,
            .cornerRadius = CLAY_CORNER_RADIUS(12),
            .border = { .width = CLAY_BORDER_OUTSIDE(1), .color = C_LINE }
        }) {
            CLAY_TEXT(CLAY_STRING(APP_NAME), CLAY_TEXT_CONFIG(TextCfg(24, C_TEXT, FONT_TITLE)));
            CLAY_TEXT(CLAY_STRING("Version " APP_VERSION), CLAY_TEXT_CONFIG(TextCfgNoWrap(16, C_TEXT_FAINT, FONT_BODY)));

            CLAY_AUTO_ID({ .layout = { .sizing = { .width = CLAY_SIZING_GROW(0), .height = CLAY_SIZING_FIXED(14) } } }) {}

            CLAY_TEXT(CLAY_STRING("PSS (PlayStation Stream) video player and editor for Twinsanity. Opens and "
                                  "saves retail .pss cutscenes, and imports/exports .mp4 video, scaling and "
                                  "stretching it to whatever resolution the game engine needs."),
                      CLAY_TEXT_CONFIG(TextCfg(16, C_TEXT_DIM, FONT_BODY)));

            CLAY_AUTO_ID({ .layout = { .sizing = { .width = CLAY_SIZING_GROW(0), .height = CLAY_SIZING_FIXED(16) } },
                           .border = { .width = { .bottom = 1 }, .color = C_LINE } }) {}

            CLAY_AUTO_ID({ .layout = { .sizing = { .width = CLAY_SIZING_GROW(0) }, .padding = { .top = 10, .bottom = 4 } } }) {
                CLAY_TEXT(CLAY_STRING("Shortcuts"), CLAY_TEXT_CONFIG(TextCfgNoWrap(16, C_TEXT, FONT_TITLE)));
            }

            RenderAboutRow(CLAY_STRING("Space"),          CLAY_STRING("Play / Pause"));
            RenderAboutRow(CLAY_STRING("Right / ."),      CLAY_STRING("Advance one frame"));
            RenderAboutRow(CLAY_STRING("Left / ,"),       CLAY_STRING("Step back one frame"));
            RenderAboutRow(CLAY_STRING("R"),              CLAY_STRING("Restart from the beginning"));
            RenderAboutRow(CLAY_STRING("Ctrl + O"),       CLAY_STRING("Open a .pss file"));
            RenderAboutRow(CLAY_STRING("Ctrl + S"),       CLAY_STRING("Save the current video"));
            RenderAboutRow(CLAY_STRING("Ctrl + W"),       CLAY_STRING("Close the current video"));
            RenderAboutRow(CLAY_STRING("Esc"),            CLAY_STRING("Close this dialog"));

            CLAY_AUTO_ID({ .layout = { .sizing = { .width = CLAY_SIZING_GROW(0), .height = CLAY_SIZING_FIXED(16) } },
                           .border = { .width = { .bottom = 1 }, .color = C_LINE } }) {}

            CLAY_AUTO_ID({
                .layout = { .sizing = { .width = CLAY_SIZING_GROW(0) }, .padding = { .top = 12 },
                            .childAlignment = { .y = CLAY_ALIGN_Y_CENTER } }
            }) {
                CLAY_TEXT(CLAY_STRING("Built with Clay, raylib, ffmpeg and tinyfiledialogs"),
                          CLAY_TEXT_CONFIG(TextCfgNoWrap(14, C_TEXT_FAINT, FONT_BODY)));

                CLAY_AUTO_ID({ .layout = { .sizing = { .width = CLAY_SIZING_GROW(0), .height = CLAY_SIZING_FIXED(1) } } }) {}

                CLAY(CLAY_ID("AboutCloseButton"), {
                    .layout = {
                        .sizing = { .width = CLAY_SIZING_FIXED(96), .height = CLAY_SIZING_FIXED(32) },
                        .childAlignment = { .x = CLAY_ALIGN_X_CENTER, .y = CLAY_ALIGN_Y_CENTER }
                    },
                    .backgroundColor = Clay_Hovered() ? (Clay_Color){120, 190, 255, 255} : C_ACCENT,
                    .cornerRadius = CLAY_CORNER_RADIUS(6)
                }) {
                    CLAY_TEXT(CLAY_STRING("Close"), CLAY_TEXT_CONFIG(TextCfgNoWrap(16, C_ON_ACCENT, FONT_BODY)));
                }
            }
        }
    }
}

static Clay_RenderCommandArray BuildLayout(float dt) {
    Clay_BeginLayout();

    CLAY(CLAY_ID("Root"), {
        .layout = {
            .sizing = { .width = CLAY_SIZING_GROW(0), .height = CLAY_SIZING_GROW(0) },
            .layoutDirection = CLAY_TOP_TO_BOTTOM
        },
        .backgroundColor = C_BG
    }) {
        RenderMenuBar();

        CLAY(CLAY_ID("Content"), {
            .layout = { .sizing = { .width = CLAY_SIZING_GROW(0), .height = CLAY_SIZING_GROW(0) } }
        }) {
            if (IsBusy()) RenderBusyOverlay();
            else RenderVideoArea();
        }

        RenderSeekBar();
        RenderAudioTrackSelector();
        RenderTransportBar();
        RenderConfirmDialog();
        RenderCompressWarningDialog();
        RenderAboutDialog();
    }

    return Clay_EndLayout(dt);
}

// ---------------------------------------------------------------------------
// Boot
// ---------------------------------------------------------------------------

static bool g_reinitClay = false;

static void HandleClayErrors(Clay_ErrorData errorData) {
    printf("clay: %.*s\n", (int)errorData.errorText.length, errorData.errorText.chars);
    if (errorData.errorType == CLAY_ERROR_TYPE_ELEMENTS_CAPACITY_EXCEEDED) {
        Clay_SetMaxElementCount(Clay_GetMaxElementCount() * 2);
        g_reinitClay = true;
    } else if (errorData.errorType == CLAY_ERROR_TYPE_TEXT_MEASUREMENT_CAPACITY_EXCEEDED) {
        Clay_SetMaxMeasureTextCacheWordCount(Clay_GetMaxMeasureTextCacheWordCount() * 2);
        g_reinitClay = true;
    }
}

static void *g_clayMemory = NULL;

static void InitClay(void) {
    uint64_t size = Clay_MinMemorySize();
    void *block = malloc(size);
    if (!block) { fprintf(stderr, "Out of memory for Clay's arena\n"); return; }

    free(g_clayMemory);
    g_clayMemory = block;

    Clay_Arena arena = Clay_CreateArenaWithCapacityAndMemory(size, block);
    Clay_Initialize(arena, (Clay_Dimensions){ (float)GetScreenWidth(), (float)GetScreenHeight() },
                    (Clay_ErrorHandler){ HandleClayErrors, 0 });
}

int main(void) {
    Clay_Raylib_Initialize(1280, 800, APP_NAME, FLAG_VSYNC_HINT | FLAG_WINDOW_RESIZABLE | FLAG_MSAA_4X_HINT);
    SetExitKey(KEY_NULL);
    InitAudioDevice();
    SetAudioStreamBufferSizeDefault((int)AUDIO_CHUNK_FRAMES);

    TsMutexInit(&g_loader.mutex);
    g_loader.state = LOAD_IDLE;

    Clay_SetMaxElementCount(4096);
    InitClay();
    TwinStudio_UiInit();

    Font *fonts = TwinStudio_GetUiContext()->fonts;

    while (!g_quit) {
        if (WindowShouldClose()) {
            if (IsBusy() || g_ui.compressWarningOpen) {
                fprintf(stderr, "Close ignored: resolve the open dialog first\n");
            } else if (g_ui.confirmOpen) {
                g_ui.pendingAction = PENDING_EXIT;
            } else {
                RequestAction(PENDING_EXIT);
            }
        }

        if (g_reinitClay) {
            InitClay();
            Clay_SetMeasureTextFunction(Raylib_MeasureText, fonts);
            g_reinitClay = false;
        }

        float   dt    = GetFrameTime();
        Vector2 mouse = GetMousePosition();

        Clay_SetLayoutDimensions((Clay_Dimensions){ (float)GetScreenWidth(), (float)GetScreenHeight() });
        Clay_SetPointerState((Clay_Vector2){ mouse.x, mouse.y }, IsMouseButtonDown(MOUSE_BUTTON_LEFT));

        PumpLoader();
        const bool busy = IsBusy();

        if (g_ui.confirmOpen) {
            HandleConfirmDialog(mouse);
        } else if (g_ui.compressWarningOpen) {
            HandleCompressWarningDialog(mouse);
        } else if (g_ui.aboutOpen) {
            HandleAboutDialog(mouse);
        } else if (!busy) {
            HandleMenuBar();
            HandleSeekBar(mouse);
            HandleVolumeSlider(mouse);
            if (!g_ui.seekDragging && !g_ui.volumeDragging) {
                HandleTransportButtons();
                HandleAudioTrackSelector();
                HandleKeyboard();
            }
        }
        UpdatePlayback(dt);

        if (g_quit) break;

        Clay_UpdateScrollContainers(false, (Clay_Vector2){ 0, 0 }, dt);

        ResetFrameStrings();
        ResetFrameCustomElements();
        Clay_RenderCommandArray commands = BuildLayout(dt);

        BeginDrawing();
        ClearBackground(BLACK);
        Clay_Raylib_Render(commands, fonts);
        EndDrawing();
    }

    if (g_loader.threadValid) {
        fprintf(stderr, "Waiting for the background job to finish...\n");
        TsThreadJoin(&g_loader.thread);
        g_loader.threadValid = false;
    }
    TsMutexFree(&g_loader.mutex);

    StopPlayback();
    if (g_doc.loaded) TwinStudio_ArenaFree(&g_doc.arena);

    CloseAudioDevice();
    Clay_Raylib_Close();
    return 0;
}
