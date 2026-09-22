#include "audio/adpcm.h"
#include "audio/wave.h"
#include "memory/memory.h"
#include "ps2/retail/archive_serializers.h"
#include "ps2/retail/auto_struct_mb_archive.h"
#include "ps2/retail/auto_struct_mh_archive.h"
#include "serialization/binary_serializer.h"
#include <limits.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdint.h>
#include <raylib.h>
#include <rpmalloc.h>
#include <cJSON.h>
#include <clay.h>
#include <tinyfiledialogs.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <math.h>
#include "string_view/string_view.h"
#include "ui/ui.h"

#define TS_RENDERER_IMPLEMENTATION
#include "render/renderer.h"

// ---------------------------------------------------------------------------
// Minimal thread + mutex shim
//
// Win32 rather than C11 <threads.h>, which MSVC still does not ship.
// NOGDI/NOUSER matter: without them windows.h redefines Rectangle,
// CloseWindow, ShowCursor, DrawText and LoadImage, all of which raylib also
// declares. WIN32_LEAN_AND_MEAN alone is not enough for that.
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

// rpmalloc needs every thread that allocates to register itself. This is on by
// default because rpmalloc.h is included above; set it to 0 if rpmalloc is not
// actually installed as the allocator, since calling these without
// rpmalloc_initialize() having run is not safe.
#ifndef USE_RPMALLOC_THREADS
    #define USE_RPMALLOC_THREADS 1
#endif


// ============================================================================
//  Clay + Raylib music player
//
//  Layout
//    left  : track details, transport, waveform scrubber
//    right : playlist with a custom scrollbar, multi selection and
//            drag & drop reordering with a live drop indicator
//
//  Build: make          (see Makefile - needs clay.h and
//                        renderers/raylib/clay_renderer_raylib.c next to this file)
//  Run:   ./player                  -> generated demo playlist
//         ./player ~/Music          -> loads audio files from a folder
//
//  Interaction
//    click                  select one
//    ctrl+click             toggle one
//    shift+click            select range
//    drag                   reorder selection (blue line = drop position)
//    double click / enter   load & play
//    space                  play / pause
//    ctrl+A                 select all
//    delete/backspace       remove selected
//    up/down (+shift)       move / extend selection
//    drag on waveform       scrub
//    ctrl+D                 clay debug view
// ============================================================================

// ---------------------------------------------------------------------------
// Items storage - fixed global array, as requested.
// ---------------------------------------------------------------------------

#define MAX_ITEMS        1024
#define WAVEFORM_BUCKETS 1024   // peak buckets kept for the *loaded* track only

// --- archive memory budgets -------------------------------------------------
//
// A record arrives as 4-bit ADPCM and comes out as 16-bit PCM: 16 bytes carry
// 28 samples, so 56 bytes come back, a 3.5x expansion. While a record is being
// read its arena holds both the raw blob and the decoded samples, so budget
// 1 + 3.5 and round up to 6 for the interleave padding and bookkeeping.
#define ADPCM_ARENA_FACTOR   6u
#define ARENA_PER_RECORD_FAT (8u * 1024u)        // name block, alignment, slack

// How much decoded audio may sit in the archive arena at once. Records are
// read in batches that fit inside this, and the arena is released between
// batches, so the arena no longer scales with the size of the archive.
// Raising it trades memory for fewer arena allocations.
#define LOAD_ARENA_BUDGET    (32u * 1024u * 1024u)

// Floors and ceilings for the arenas whose size is now derived from the data.
// The ceilings are the constants that used to be hard-coded, so nothing can
// end up asking for more than the version before this did.
// Decoded tracks kept around beyond the one being played. Raising this trades
// memory for fewer decodes when stepping back and forth through the list; one
// stereo track is a few MB, so this holds a good handful.
#define PCM_CACHE_BUDGET     (64u * 1024u * 1024u)

#define HEADER_ARENA_MIN     (1u * 1024u * 1024u)
#define HEADER_ARENA_MAX     (32u * 1024u * 1024u)
#define WRITE_ARENA_MIN      (64u * 1024u * 1024u)
#define WRITE_ARENA_MAX      (300u * 1024u * 1024u)
#define TRACK_ARENA_MIN      (1u * 1024u * 1024u)
#define TRACK_ARENA_MAX      (20u * 1024u * 1024u)

static size_t ClampSize(size_t v, size_t lo, size_t hi) {
    return v < lo ? lo : (v > hi ? hi : v);
}

// A playlist slot is either real audio or a null/padding record. Null slots
// are not filler in the UI sense: the game engine addresses tracks by their
// index in the archive, so every one of them has to survive a round trip and
// keep its position, or playback in game shifts.
typedef enum {
    TRACK_KIND_AUDIO = 0,
    TRACK_KIND_NULL
} TrackKind;

typedef struct {
    char     title[256];
    char     artist[64];
    char     album[64];
    char     path[416];
    char     format[12];
    int      year;
    float    duration;      // seconds
    uint32_t uid;           // stable identity, survives reordering
    bool     hasFile;       // false => synthesized demo entry
    bool     selected;
    TrackKind kind;         // TRACK_KIND_NULL => padding slot, holds no audio

    // Source bytes: the record's ADPCM exactly as it sits in the .MB, minus the
    // 0x30 name block that a mono record carries in front of it. This is what
    // the row actually holds - 4-bit ADPCM, roughly 3.5x smaller than the
    // 16-bit PCM it decodes to - and it is shared between rows that address
    // the same blob. NULL for a padding slot and for an imported MP3/WAV,
    // which arrives as PCM and has no ADPCM form until the archive is written.
    uint8_t *adpcm;
    uint32_t adpcmSize;
    uint32_t interleave;    // bytes per channel chunk, 0 => mono (decoder's rule)
    uint32_t pcmSize;       // what the decode is expected to produce, for checking

    // Decoded PCM, as a RIFF buffer. This is a *cache*: NULL until something
    // needs samples, and dropped again once the cache is over budget. An
    // imported track has no ADPCM to rebuild it from, so its buffer is the
    // only copy and is never evicted. Whatever is playing streams straight out
    // of this, so it must outlive the stream reading it.
    uint8_t *wav;
    uint32_t wavSize;
    uint64_t pcmStamp;      // last use, for eviction order
    uint32_t sampleRate;
    uint8_t  channels;
    int32_t  loopPosition;  // < 0 when the track has no loop point

    // Retail's per-record header field that never correlates with anything
    // else in the record - not size, offset, sample rate, or the ADPCM bytes
    // themselves (checked: duplicate values across tracks don't share audio
    // content either). Only non-zero on stereo BGM, so it reads as mixing/
    // mastering metadata set by hand rather than anything this tool derives.
    // Carried through untouched so a round-tripped track keeps its original
    // value instead of always coming back as 0; a genuinely new/imported
    // track has no original to preserve and gets 0, same as before.
    int32_t  unkInt;
} Item;

static Item g_items[MAX_ITEMS];
static int  g_itemCount = 0;
static Item g_scratch[MAX_ITEMS];   // used by the reorder pass
static uint32_t g_nextUid = 1;

// ---------------------------------------------------------------------------
// Background archive loading
//
// The worker never touches g_items. It fills a separate staging array, and the
// main thread copies that across in one step once the worker has finished, so
// no list the UI walks is ever half-written. Everything shared is behind one
// mutex, locked once per frame.
// ---------------------------------------------------------------------------

typedef enum {
    LOAD_IDLE = 0,
    LOAD_RUNNING,
    LOAD_DONE,        // worker finished, main thread still has to publish
    LOAD_FAILED
} LoadState;

// Opening and saving share this state machine: same progress reporting, same
// "the UI is frozen until it finishes" handling. Only the worker differs.
typedef enum {
    JOB_NONE = 0,
    JOB_OPEN,
    JOB_SAVE
} JobKind;

typedef struct {
    TsMutex   mutex;
    TsThread  thread;
    bool      threadValid;

    // --- guarded by mutex ---
    LoadState state;
    JobKind   job;
    int       done;             // records processed so far
    int       total;            // records in the archive, 0 until the header is read
    char      status[96];       // phase, shown under the bar
    char      error[160];

    // --- worker-owned until state leaves LOAD_RUNNING ---
    char      path[1024];       // archive to open
    char      savePathHeader[1024];
    char      savePathMain[1024];
} ArchiveLoader;

static ArchiveLoader g_loader;
static Item g_loadStaging[MAX_ITEMS];   // built by the worker, published by main
static int  g_loadStagingCount = 0;

static LoadState LoaderState(void) {
    TsMutexLock(&g_loader.mutex);
    LoadState s = g_loader.state;
    TsMutexUnlock(&g_loader.mutex);
    return s;
}

static bool IsLoading(void) { return LoaderState() == LOAD_RUNNING; }

// Any background job at all. While this is true every interactive handler is
// skipped, because both workers read g_items and neither tolerates it moving.
static bool IsBusy(void) { return IsLoading(); }

static JobKind CurrentJob(void) {
    TsMutexLock(&g_loader.mutex);
    JobKind j = g_loader.job;
    TsMutexUnlock(&g_loader.mutex);
    return j;
}

static void LoaderSetStatus(const char *text, int done, int total) {
    TsMutexLock(&g_loader.mutex);
    snprintf(g_loader.status, sizeof(g_loader.status), "%s", text);
    if (done  >= 0) g_loader.done  = done;
    if (total >= 0) g_loader.total = total;
    TsMutexUnlock(&g_loader.mutex);
}

static void LoaderSetProgress(int done, int total) {
    TsMutexLock(&g_loader.mutex);
    g_loader.done = done;
    if (total >= 0) g_loader.total = total;
    TsMutexUnlock(&g_loader.mutex);
}

static void LoaderFinish(LoadState state, const char *error) {
    TsMutexLock(&g_loader.mutex);
    g_loader.state = state;
    if (error) snprintf(g_loader.error, sizeof(g_loader.error), "%s", error);
    TsMutexUnlock(&g_loader.mutex);
}

// ---------------------------------------------------------------------------
// Player
// ---------------------------------------------------------------------------

// Tracks are streamed from their own PCM rather than through raylib's Music
// API. That is what makes the archive loop points usable: the sample fed after
// the last one is chosen here, so a loop restarts exactly at its loop frame
// with no gap and no re-decode. It also means the play position is counted in
// frames we pushed ourselves, instead of read back from GetMusicTimePlayed,
// which wraps modulo the track length near the end.
#define STREAM_CHUNK_FRAMES   2048u
#define STREAM_MAX_CHANNELS   8u
// Two sub-buffers sit between what has been pushed and what is audible.
#define STREAM_LATENCY_FRAMES (2u * STREAM_CHUNK_FRAMES)

typedef struct {
    uint32_t uid;                        // 0 = nothing loaded
    bool     playing;
    float    position;
    float    duration;
    float    peaks[WAVEFORM_BUCKETS];
    int      peakCount;
    float    volume;

    // --- raw PCM streaming ---
    AudioStream    stream;
    bool           hasStream;
    bool           streamStarted;        // Play vs Resume, and reset by a seek
    const int16_t *pcm;                  // borrowed from the item's wav buffer
    uint32_t       frameCount;
    uint32_t       sampleRate;
    uint8_t        channels;

    uint32_t       playCursor;           // next frame to hand to the stream
    uint32_t       anchorFrame;          // cursor when pushing last (re)started
    uint64_t       framesPushed;         // frames pushed since that anchor
    bool           endPushed;            // ran off the end with looping off

    // --- loop point ---
    bool           loopEnabled;          // this track loops
    uint32_t       loopFrame;            // where it loops back to
    bool           loopWanted;           // user toggle, remembered across tracks
} Player;

static Player g_player = { .volume = 0.7f, .loopWanted = true };

// Scratch buffer for one push. Static: it is only ever touched on the main
// thread, between IsAudioStreamProcessed and UpdateAudioStream.
static int16_t g_pushBuffer[STREAM_CHUNK_FRAMES * STREAM_MAX_CHANNELS];

// ---------------------------------------------------------------------------
// UI / interaction state
// ---------------------------------------------------------------------------

// What the confirmation dialog is standing in front of. The playlist only
// ever gets thrown away by these two, so they are the only things gated.
typedef enum {
    PENDING_NONE = 0,
    PENDING_OPEN,     // open another archive, which drops the one in memory
    PENDING_EXIT      // close the window
} PendingAction;

typedef struct {
    // list dragging
    bool  pressedInList;
    bool  dragCandidate;
    bool  dragging;
    int   pressIndex;
    Vector2 pressPos;
    bool  pressWasSelected;
    int   dropIndex;
    int   anchorIndex;
    int   hoverIndex;
    // double click
    double lastClickTime;
    int    lastClickIndex;
    // scrollbar
    bool  scrollbarDrag;
    float scrollGrabY;
    float scrollOriginY;
    // waveform
    bool  scrubbing;
    float scrubPosition;
    // volume slider
    bool  volumeDrag;
    // About dialog. Modal: while it is up nothing behind it takes input.
    bool  aboutOpen;
    // Unsaved-changes dialog. Also modal, and it outranks the About dialog.
    bool  confirmOpen;
    PendingAction pendingAction;   // what the dialog is asking about
    PendingAction afterSaveAction; // what to do once a save started from it lands
    // row to bring into view once it has been through a layout pass
    int   pendingScrollIndex;
} UIState;

static UIState g_ui = { .dropIndex = -1, .anchorIndex = -1, .hoverIndex = -1,
                        .lastClickIndex = -1, .pendingScrollIndex = -1 };

// The playlist has changes that are not on disk. Set by every mutation,
// cleared by a successful save and by dropping the playlist on open.
static bool g_dirty = false;

// Set instead of leaving the loop directly, so the close request can go
// through the confirmation dialog first.
static bool g_quit = false;

static void MarkDirty(void) { g_dirty = true; }

static Font *g_fonts = NULL;   // [FONT_BODY], [FONT_TITLE]

// ---------------------------------------------------------------------------
// Theme
// ---------------------------------------------------------------------------

#define C_BG         (Clay_Color){ 17,  18,  23, 255}
#define C_PANEL      (Clay_Color){ 24,  26,  33, 255}
#define C_PANEL_2    (Clay_Color){ 31,  34,  43, 255}
#define C_ROW        (Clay_Color){ 26,  28,  36, 255}
#define C_ROW_ALT    (Clay_Color){ 29,  32,  40, 255}
#define C_HOVER      (Clay_Color){ 43,  47,  59, 255}
#define C_SELECT     (Clay_Color){ 38,  62,  97, 255}
#define C_SELECT_HOV (Clay_Color){ 46,  74, 115, 255}
#define C_ACCENT     (Clay_Color){ 96, 174, 255, 255}
#define C_ACCENT_DIM (Clay_Color){ 60, 110, 165, 255}
#define C_TEXT       (Clay_Color){231, 235, 243, 255}
#define C_TEXT_DIM   (Clay_Color){145, 152, 168, 255}
#define C_TEXT_FAINT (Clay_Color){ 96, 103, 118, 255}
#define C_WAVE       (Clay_Color){ 62,  68,  84, 255}
#define C_LINE       (Clay_Color){ 44,  48,  60, 255}

#define FONT_BODY  0
#define FONT_TITLE 0

#define APP_NAME     "Twinsanity Music/VA Manager"
#define APP_VERSION  "1.0"

#define LIST_WIDTH   400.0f
#define MENU_H        44.0f
#define ROW_HEIGHT    52.0f
#define WAVE_H       120.0f
#define WAVE_PAD      12.0f
#define SCROLLBAR_W   10.0f
#define VOL_W        120.0f

#define ROW_PAD_X     12.0f
#define ROW_NUM_W     26.0f
#define ROW_GAP       10.0f

// ---------------------------------------------------------------------------
// Per-frame string arena (Clay only stores pointers, so strings must outlive
// the layout pass - this is reset once per frame before Clay_BeginLayout).
// ---------------------------------------------------------------------------

static char g_strArena[256 * 1024];
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

// Borrows the string: Clay keeps the pointer and reads it during rendering,
// after the declaring function has returned. Only pass storage that outlives
// the frame (g_items fields, literals). For anything on the stack use Fmt(),
// which copies into the frame arena.
static Clay_String Str(const char *s) {
    return (Clay_String){ .length = (int32_t)strlen(s), .chars = s };
}

static Clay_String TimeStr(float seconds) {
    if (seconds < 0 || seconds != seconds) seconds = 0;
    int total = (int)seconds;
    return Fmt("%d:%02d", total / 60, total % 60);
}

// Truncate with an ellipsis so a long title can never bleed over its
// neighbours. Note the budget passed in must not depend on the measured text
// width, or the layout would oscillate between frames.
static Clay_String FitText(const char *text, float fontSize, float maxWidth) {
    Font font = g_fonts ? g_fonts[FONT_BODY] : GetFontDefault();
    if (!font.glyphs) font = GetFontDefault();

    int len = (int)strlen(text);
    if (len == 0) return (Clay_String){ .length = 0, .chars = "" };
    if (maxWidth <= 4.0f) return (Clay_String){ .length = 0, .chars = "" };

    float full = MeasureTextEx(font, text, fontSize, 0).x;
    if (full <= maxWidth) return Str(text);

    char *dst = g_strArena + g_strUsed;
    int avail = (int)sizeof(g_strArena) - g_strUsed;
    if (avail < len + 5) return Str(text);          // arena exhausted, accept overflow

    int n = (int)((float)len * (maxWidth / full));  // proportional first guess
    if (n > len) n = len;
    if (n < 1)   n = 1;

    for (; n > 1; n--) {
        memcpy(dst, text, (size_t)n);
        dst[n] = '.'; dst[n + 1] = '.'; dst[n + 2] = '.'; dst[n + 3] = '\0';
        if (MeasureTextEx(font, dst, fontSize, 0).x <= maxWidth) break;
    }
    memcpy(dst, text, (size_t)n);
    dst[n] = '.'; dst[n + 1] = '.'; dst[n + 2] = '.'; dst[n + 3] = '\0';
    g_strUsed += n + 4;
    return (Clay_String){ .length = n + 3, .chars = dst };
}

// ---------------------------------------------------------------------------
// Small helpers
// ---------------------------------------------------------------------------

static float Clampf(float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); }

static uint32_t Hash32(uint32_t x) {
    x ^= x >> 16; x *= 0x7feb352dU;
    x ^= x >> 15; x *= 0x846ca68bU;
    x ^= x >> 16;
    return x;
}
static float Hash01(uint32_t x) { return (float)(Hash32(x) & 0xFFFFFF) / (float)0xFFFFFF; }

static bool PointInBox(Clay_BoundingBox b, Vector2 p) {
    return p.x >= b.x && p.x <= b.x + b.width && p.y >= b.y && p.y <= b.y + b.height;
}

static bool CtrlDown(void) {
    return IsKeyDown(KEY_LEFT_CONTROL) || IsKeyDown(KEY_RIGHT_CONTROL) ||
           IsKeyDown(KEY_LEFT_SUPER)   || IsKeyDown(KEY_RIGHT_SUPER);
}
static bool ShiftDown(void) { return IsKeyDown(KEY_LEFT_SHIFT) || IsKeyDown(KEY_RIGHT_SHIFT); }

static int SelectedCount(void) {
    int n = 0;
    for (int i = 0; i < g_itemCount; i++) if (g_items[i].selected) n++;
    return n;
}

static int FirstSelected(void) {
    for (int i = 0; i < g_itemCount; i++) if (g_items[i].selected) return i;
    return -1;
}

static int LastSelected(void) {
    for (int i = g_itemCount - 1; i >= 0; i--) if (g_items[i].selected) return i;
    return -1;
}

static int IndexOfUid(uint32_t uid) {
    if (!uid) return -1;
    for (int i = 0; i < g_itemCount; i++) if (g_items[i].uid == uid) return i;
    return -1;
}

static bool IsPlayable(int index) {
    return index >= 0 && index < g_itemCount && g_items[index].kind == TRACK_KIND_AUDIO;
}

// Walk in `dir` from `from` (inclusive) to the first slot that holds audio.
// Null slots are skipped for playback while keeping their place in the list.
static int NextPlayableIndex(int from, int dir) {
    for (int i = from; i >= 0 && i < g_itemCount; i += dir) if (IsPlayable(i)) return i;
    return -1;
}

static int NullCount(void) {
    int n = 0;
    for (int i = 0; i < g_itemCount; i++) if (g_items[i].kind == TRACK_KIND_NULL) n++;
    return n;
}

// ---------------------------------------------------------------------------
// Waveform
// ---------------------------------------------------------------------------

// Deterministic stand-in waveform so the UI is fully usable without assets.
static void SynthesizeWaveform(const Item *item, float *out, int count) {
    uint32_t seed = item->uid * 2654435761u;
    int sections = 4 + (int)(Hash01(seed) * 4.0f);
    for (int i = 0; i < count; i++) {
        float t = (float)i / (float)(count > 1 ? count - 1 : 1);
        int   sec = (int)(t * sections);
        float secLoud = 0.35f + 0.65f * Hash01(seed + (uint32_t)sec * 977u);
        float beat = 0.55f + 0.45f * fabsf(sinf(t * 160.0f + Hash01(seed) * 6.28f));
        float noise = 0.65f + 0.35f * Hash01(seed + (uint32_t)i * 2654435761u);
        float attack = Clampf(t * 18.0f, 0.0f, 1.0f);
        float release = Clampf((1.0f - t) * 14.0f, 0.0f, 1.0f);
        out[i] = Clampf(secLoud * beat * noise * attack * release, 0.02f, 1.0f);
    }
}

// Peaks straight out of interleaved 16-bit PCM. No decode, no file access -
// this is what archive tracks use, since their samples are already in memory.
static bool ComputeWaveformFromPcm16(const void *pcm, uint32_t pcmSize, uint8_t channels,
                                     float *out, int count) {
    if (!pcm || pcmSize < 2 || channels == 0) return false;

    const int16_t *samples = (const int16_t *)pcm;
    unsigned long long frames = pcmSize / (unsigned long long)(channels * sizeof(int16_t));
    if (frames == 0) return false;

    float peak = 0.0f;
    for (int b = 0; b < count; b++) {
        unsigned long long from = (unsigned long long)((double)frames * b / count);
        unsigned long long to   = (unsigned long long)((double)frames * (b + 1) / count);
        if (to <= from) to = from + 1;
        if (to > frames) to = frames;

        float maxAbs = 0.0f;
        unsigned long long step = (to - from) / 512 + 1;   // subsample, keeps it fast
        for (unsigned long long f = from; f < to; f += step) {
            float v = fabsf((float)samples[f * channels] / 32768.0f);
            if (v > maxAbs) maxAbs = v;
        }
        out[b] = maxAbs;
        if (maxAbs > peak) peak = maxAbs;
    }

    if (peak > 0.0001f) for (int b = 0; b < count; b++) out[b] = Clampf(out[b] / peak, 0.02f, 1.0f);
    return true;
}

// ---------------------------------------------------------------------------
// In-memory track audio
//
// TwinStudio_Wave carries raw samples with no bit depth field, so the decoded
// PS2 track data is taken to be signed 16-bit PCM. If that ever changes this
// constant and ComputeWaveformFromPcm16 are the only two places that care.
// ---------------------------------------------------------------------------

#define TRACK_BITS_PER_SAMPLE 16

static void PutU32LE(uint8_t *p, uint32_t v) {
    p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); p[2] = (uint8_t)(v >> 16); p[3] = (uint8_t)(v >> 24);
}
static void PutU16LE(uint8_t *p, uint16_t v) {
    p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8);
}

// Both kinds of track buffer - the source ADPCM and the decoded PCM - are
// refcounted, because several header records can address the same blob in the
// .MB. Every repeated "undefined" entry points at one, which is why
// SaveArchives writes it once and reuses the record, and holding a copy per
// record stored the same audio over and over.
//
// The count sits in a 16 byte prefix, so the payload stays 16 byte aligned and
// the pointer handed out is an ordinary uint8_t* that the player, the exporter
// and the serializer can use without knowing any of this. Not atomic: the
// loader thread owns every buffer until the playlist is published, and only
// one thread touches them afterwards (the UI is frozen while a job runs).
typedef struct { uint32_t refs; uint32_t size; } Blob;
#define BLOB_PREFIX 16

static uint8_t *BlobAlloc(uint32_t size) {
    uint8_t *base = (uint8_t *)malloc((size_t)BLOB_PREFIX + size);
    if (!base) return NULL;
    Blob *b = (Blob *)base;
    b->refs = 1;
    b->size = size;
    return base + BLOB_PREFIX;
}

static Blob *BlobHeaderOf(uint8_t *wav) { return (Blob *)(wav - BLOB_PREFIX); }

static uint8_t *BlobRetain(uint8_t *wav) {
    if (wav) BlobHeaderOf(wav)->refs++;
    return wav;
}

static void BlobRelease(uint8_t *wav) {
    if (!wav) return;
    Blob *b = BlobHeaderOf(wav);
    if (--b->refs == 0) free(b);
}

// Wrap raw PCM in a 44 byte RIFF/WAVE header so raylib can stream it from
// memory. The samples are copied, so the source (an arena that gets freed as
// soon as the archive is closed) does not need to stay alive. The returned
// buffer is owned by the caller. Fields are written byte by byte, so this is
// correct on a big endian host too.
static uint8_t *WrapPcmAsWav(const void *pcm, uint32_t pcmSize, uint32_t sampleRate,
                             uint8_t channels, uint32_t *outSize) {
    if (!pcm || pcmSize == 0 || sampleRate == 0 || channels == 0) return NULL;

    const uint16_t bits       = TRACK_BITS_PER_SAMPLE;
    const uint16_t blockAlign = (uint16_t)(channels * (bits / 8));
    const uint32_t total      = 44u + pcmSize;

    uint8_t *buf = BlobAlloc(total);
    if (!buf) return NULL;

    memcpy(buf +  0, "RIFF", 4);
    PutU32LE(buf +  4, 36u + pcmSize);            // size of everything after this field
    memcpy(buf +  8, "WAVE", 4);
    memcpy(buf + 12, "fmt ", 4);
    PutU32LE(buf + 16, 16);                       // PCM fmt chunk size
    PutU16LE(buf + 20, 1);                        // format tag: PCM
    PutU16LE(buf + 22, channels);
    PutU32LE(buf + 24, sampleRate);
    PutU32LE(buf + 28, sampleRate * blockAlign);  // byte rate
    PutU16LE(buf + 32, blockAlign);
    PutU16LE(buf + 34, bits);
    memcpy(buf + 36, "data", 4);
    PutU32LE(buf + 40, pcmSize);
    memcpy(buf + 44, pcm, pcmSize);

    if (outSize) *outSize = total;
    return buf;
}

// ---------------------------------------------------------------------------
// Decode cache
//
// A row holds its ADPCM and decodes on demand. Holding every track decoded
// cost 3.5x this and most of it was never listened to in a given session.
// ---------------------------------------------------------------------------

static uint64_t g_pcmClock = 1;      // bumped on every use, for eviction order
static uint64_t g_pcmHeld  = 0;      // bytes of decoded PCM currently cached
static bool     g_pcmPinAll = false; // set while saving, which needs them all at once

// Deliberately reads nothing the save worker writes: it runs on another thread
// and fills item->wav in as it goes, while the main thread is still drawing.
static bool ItemHasAudio(const Item *item) {
    return item && item->kind != TRACK_KIND_NULL && (item->adpcm || item->pcmSize);
}

// An imported MP3/WAV has no ADPCM behind it, so its buffer is the only copy
// and dropping it would lose the track. Only decodes are reclaimable.
static bool ItemPcmIsReclaimable(const Item *item) {
    return item && item->wav && item->adpcm;
}

static void DropItemPcm(Item *item) {
    if (!item || !item->wav) return;
    g_pcmHeld -= (g_pcmHeld >= item->wavSize) ? item->wavSize : g_pcmHeld;
    BlobRelease(item->wav);
    item->wav = NULL;
    item->wavSize = 0;
}

// Bring the cache back under budget, oldest first. `keep` is the row being
// decoded right now and `playing` is whatever the audio thread is streaming
// out of - freeing either would pull memory out from under a live reader.
static void TrimPcmCache(const Item *keep) {
    if (g_pcmPinAll) return;

    int playing = IndexOfUid(g_player.uid);

    while (g_pcmHeld > PCM_CACHE_BUDGET) {
        int      oldest = -1;
        uint64_t oldestStamp = UINT64_MAX;

        for (int i = 0; i < g_itemCount; i++) {
            Item *it = g_items + i;
            if (it == keep || i == playing) continue;
            if (!ItemPcmIsReclaimable(it)) continue;
            if (it->pcmStamp < oldestStamp) { oldestStamp = it->pcmStamp; oldest = i; }
        }

        if (oldest < 0) break;          // nothing left that may be dropped
        DropItemPcm(g_items + oldest);
    }
}

// End of a save: eviction comes back on and the decodes it had to keep are
// released down to the budget again.
static void UnpinPcmCache(void) {
    g_pcmPinAll = false;
    TrimPcmCache(NULL);
}

// The one place that talks to the decoder. `interleave` goes across as the
// header's byte count, which is the units the decoder works in - it divides by
// 16 itself - and 0 means mono, per its contract.
static uint8_t *DecodeItemPcm(const Item *item, uint32_t *outSize) {
    if (!item->adpcm || item->adpcmSize == 0) return NULL;

    // The decode lands in an arena, so it is copied into the item's own buffer
    // and the arena goes away immediately. 4-bit in, 16-bit out is 3.5x; 6x
    // leaves room for the interleave padding and the decoder's bookkeeping.
    size_t arenaBytes = (size_t)item->adpcmSize * ADPCM_ARENA_FACTOR + ARENA_PER_RECORD_FAT;
    TwinStudio_Arena arena = TwinStudio_CreateArena(arenaBytes);

    TwinStudio_AdpcmDecodeResult res =
        TwinStudio_AdpcmDecode(&arena, item->adpcm, item->adpcmSize, item->interleave);

    uint8_t *wav = NULL;
    if (res.pcmData && res.pcmDataSize > 0) {
        // The size this produced was recorded when the archive was opened. A
        // mismatch means the decode ran with different parameters than the
        // load did - almost always a wrong interleave - and that is worth
        // saying out loud rather than playing something that sounds wrong.
        if (item->pcmSize && (uint32_t)res.pcmDataSize != item->pcmSize) {
            fprintf(stderr, "Decode of \"%s\" produced %llu bytes, expected %u\n",
                    item->title, (unsigned long long)res.pcmDataSize, item->pcmSize);
        }
        wav = WrapPcmAsWav(res.pcmData, (uint32_t)res.pcmDataSize,
                           item->sampleRate ? item->sampleRate : 44100,
                           item->channels ? item->channels : 1, outSize);
    }

    TwinStudio_ArenaFree(&arena);
    return wav;
}

// Make sure this row has samples, decoding them if it does not. Every consumer
// of item->wav goes through here first.
static bool EnsureItemPcm(Item *item) {
    if (!item || item->kind == TRACK_KIND_NULL) return false;

    if (item->wav && item->wavSize > 44) {
        item->pcmStamp = g_pcmClock++;
        return true;
    }
    if (!item->adpcm) return false;

    uint32_t wavSize = 0;
    uint8_t *wav = DecodeItemPcm(item, &wavSize);
    if (!wav) {
        fprintf(stderr, "Could not decode \"%s\"\n", item->title);
        return false;
    }

    item->wav      = wav;
    item->wavSize  = wavSize;
    item->pcmStamp = g_pcmClock++;
    g_pcmHeld     += wavSize;

    TrimPcmCache(item);
    return true;
}

// ---------------------------------------------------------------------------
// Playback
// ---------------------------------------------------------------------------

// The archive stores a loop point as a sample index. It is read here as a
// frame index (one frame = one sample per channel); if an archive turns out to
// store it interleaved, divide by channels in this one place.
static uint32_t LoopFrameOf(const Item *item, uint32_t frameCount) {
    if (!item || item->loopPosition <= 0) return 0;
    uint32_t frame = (uint32_t)item->loopPosition;
    return (frame < frameCount) ? frame : 0;     // out of range = no usable loop
}

static bool TrackHasLoop(const Item *item) {
    // Metadata only: asking this must never force a decode, and it must not
    // read the cache either - it runs in the render path while a save worker
    // may be filling item->wav in on the other thread.
    return ItemHasAudio(item);
}

// Map "frames pushed since the anchor" onto a position inside the track,
// folding through the loop point so the readout wraps where the audio does.
static uint32_t StreamFrameAt(uint32_t anchorFrame, uint64_t framesPushed, uint32_t latency,
                              uint32_t frameCount, uint32_t loopFrame, bool looping) {
    if (frameCount == 0) return 0;

    uint64_t played = (framesPushed > latency) ? framesPushed - latency : 0;
    uint64_t total  = (uint64_t)anchorFrame + played;

    if (total < frameCount) return (uint32_t)total;
    if (looping && frameCount > loopFrame) {
        uint32_t span = frameCount - loopFrame;
        return loopFrame + (uint32_t)((total - frameCount) % span);
    }
    return frameCount;
}

static float StreamPositionSeconds(void) {
    if (!g_player.hasStream || g_player.sampleRate == 0) return g_player.position;
    uint32_t frame = StreamFrameAt(g_player.anchorFrame, g_player.framesPushed,
                                   STREAM_LATENCY_FRAMES, g_player.frameCount,
                                   g_player.loopFrame, g_player.loopEnabled);
    return (float)frame / (float)g_player.sampleRate;
}

static void UnloadCurrent(void) {
    if (g_player.hasStream) {
        StopAudioStream(g_player.stream);
        UnloadAudioStream(g_player.stream);
        g_player.hasStream = false;
    }
    g_player.streamStarted = false;
    g_player.pcm           = NULL;
    g_player.frameCount    = 0;
    g_player.playCursor    = 0;
    g_player.anchorFrame   = 0;
    g_player.framesPushed  = 0;
    g_player.endPushed     = false;
    g_player.loopEnabled   = false;
    g_player.loopFrame     = 0;
    g_player.playing       = false;
}

static void LoadTrack(int index, bool autoplay) {
    if (index < 0 || index >= g_itemCount) return;
    if (g_items[index].kind == TRACK_KIND_NULL) return;   // nothing to play in a padding slot
    Item *item = &g_items[index];

    UnloadCurrent();
    g_player.uid       = item->uid;
    g_player.position  = 0.0f;
    g_player.duration  = item->duration;
    g_player.peakCount = WAVEFORM_BUCKETS;

    bool gotWave = false;

    // The samples may not exist yet. This is the point they get decoded, and
    // the buffer stays pinned for as long as the stream is reading out of it.
    if (EnsureItemPcm(item) && item->wav && item->wavSize > 44) {
        uint8_t  channels   = item->channels ? item->channels : 1;
        uint32_t sampleRate = item->sampleRate ? item->sampleRate : 44100;
        if (channels > STREAM_MAX_CHANNELS) channels = STREAM_MAX_CHANNELS;

        uint32_t bytes  = item->wavSize - 44u;
        uint32_t frames = bytes / (channels * (TRACK_BITS_PER_SAMPLE / 8));

        if (frames > 0) {
            g_player.pcm         = (const int16_t *)(const void *)(item->wav + 44);
            g_player.frameCount  = frames;
            g_player.sampleRate  = sampleRate;
            g_player.channels    = channels;
            g_player.playCursor  = 0;
            g_player.anchorFrame = 0;
            g_player.framesPushed = 0;
            g_player.endPushed   = false;

            g_player.loopFrame   = LoopFrameOf(item, frames);
            g_player.loopEnabled = g_player.loopWanted;

            g_player.duration = (float)frames / (float)sampleRate;
            item->duration    = g_player.duration;

            g_player.stream = LoadAudioStream(sampleRate, TRACK_BITS_PER_SAMPLE, channels);
            g_player.hasStream = true;
            SetAudioStreamVolume(g_player.stream, g_player.volume);
        }

        gotWave = ComputeWaveformFromPcm16(item->wav + 44, item->wavSize - 44,
                                           item->channels ? item->channels : 1,
                                           g_player.peaks, g_player.peakCount);
    }
    if (!gotWave) SynthesizeWaveform(item, g_player.peaks, g_player.peakCount);

    if (autoplay && g_player.hasStream) {
        g_player.playing = true;
        PlayAudioStream(g_player.stream);
        g_player.streamStarted = true;
    }
}

static void TogglePlay(void) {
    if (!g_player.uid) {
        int idx = FirstSelected();
        if (!IsPlayable(idx)) idx = NextPlayableIndex(idx >= 0 ? idx : 0, 1);
        if (idx < 0) idx = NextPlayableIndex(0, 1);
        if (idx >= 0) LoadTrack(idx, true);
        return;
    }
    g_player.playing = !g_player.playing;
    if (g_player.hasStream) {
        if (!g_player.playing) {
            PauseAudioStream(g_player.stream);
        } else if (g_player.streamStarted) {
            ResumeAudioStream(g_player.stream);
        } else {
            PlayAudioStream(g_player.stream);
            g_player.streamStarted = true;
        }
    }
}

static void SetVolume(float v) {
    g_player.volume = Clampf(v, 0.0f, 1.0f);
    if (g_player.hasStream) SetAudioStreamVolume(g_player.stream, g_player.volume);
}

static void SeekTo(float seconds) {
    g_player.position = Clampf(seconds, 0.0f, g_player.duration);
    if (!g_player.hasStream || g_player.sampleRate == 0) return;

    uint32_t frame = (uint32_t)(g_player.position * (float)g_player.sampleRate);
    if (frame >= g_player.frameCount) frame = g_player.frameCount ? g_player.frameCount - 1 : 0;

    g_player.playCursor   = frame;
    g_player.anchorFrame  = frame;
    g_player.framesPushed = 0;
    g_player.endPushed    = false;

    // Drop whatever was already queued, otherwise the old audio keeps playing
    // for a couple of buffers after the jump.
    StopAudioStream(g_player.stream);
    g_player.streamStarted = false;
    if (g_player.playing) {
        PlayAudioStream(g_player.stream);
        g_player.streamStarted = true;
    }
}

// Re-anchor the position accounting onto the frame that is audible right now.
//
// StreamFrameAt folds `framesPushed` differently depending on the loop flag:
// with looping on it wraps into [loopFrame, frameCount), with it off it clamps
// at frameCount. Once a track has run past its first pass those two readings
// disagree, so flipping the flag without rebasing makes the whole player jump
// between the real position and the end of the track on every press.
static void RebaseStreamPosition(void) {
    if (!g_player.hasStream) return;

    uint32_t current = StreamFrameAt(g_player.anchorFrame, g_player.framesPushed,
                                     STREAM_LATENCY_FRAMES, g_player.frameCount,
                                     g_player.loopFrame, g_player.loopEnabled);

    // Start counting again from here: latency in, nothing played out yet.
    g_player.anchorFrame  = current;
    g_player.framesPushed = STREAM_LATENCY_FRAMES;
}

// Turn looping on or off for the track that is loaded, without interrupting it.
static void SetLoopWanted(bool wanted) {
    bool nowEnabled = wanted && g_player.hasStream;

    if (nowEnabled != g_player.loopEnabled) {
        if (nowEnabled && g_player.endPushed) {
            // The track had already run out, so there is no position to hold
            // on to. Enabling the loop picks it up again at the loop point
            // instead of leaving it stranded at the end.
            g_player.playCursor   = g_player.loopFrame;
            g_player.anchorFrame  = g_player.loopFrame;
            g_player.framesPushed = STREAM_LATENCY_FRAMES;
            g_player.endPushed    = false;
        } else {
            // Rebase while the old flag is still set, so the reading that the
            // UI shows carries across the change unchanged.
            RebaseStreamPosition();
        }
    }

    g_player.loopWanted  = wanted;
    g_player.loopEnabled = nowEnabled;
}

static void Skip(int delta) {
    int idx = IndexOfUid(g_player.uid);
    int next = NextPlayableIndex((idx < 0) ? 0 : idx + delta, delta >= 0 ? 1 : -1);
    if (next >= 0) LoadTrack(next, true);
}

// End of track: advance to the next slot holding audio, or stop cleanly when
// there is none left after this one.
static void AdvanceOrStop(void) {
    g_player.playing = false;
    g_player.position = g_player.duration;
    if (g_player.hasStream) {
        PauseAudioStream(g_player.stream);
    }
}

// Fill one chunk from the PCM, following the loop point. This is the only
// place the loop is applied: the frame after the last one is loopFrame rather
// than nothing, so the join is sample exact with no gap and no re-decode.
// Returns true if the end was reached with looping off, in which case the
// remainder of the chunk is silence.
static bool FillStreamChunk(int16_t *dst, uint32_t frames,
                            const int16_t *pcm, uint32_t frameCount, uint32_t channels,
                            uint32_t loopFrame, bool looping,
                            uint32_t *cursor, uint64_t *framesPushed) {
    uint32_t need = frames;
    bool ended = false;

    while (need > 0) {
        if (*cursor >= frameCount) {
            if (looping && loopFrame < frameCount) {
                *cursor = loopFrame;
            } else {
                memset(dst, 0, (size_t)need * channels * sizeof(int16_t));
                ended = true;
                break;
            }
        }

        uint32_t avail = frameCount - *cursor;
        uint32_t take  = (avail < need) ? avail : need;

        memcpy(dst, pcm + (size_t)(*cursor) * channels,
               (size_t)take * channels * sizeof(int16_t));

        dst  += (size_t)take * channels;
        need -= take;
        *cursor += take;
        *framesPushed += take;
    }
    return ended;
}

// Refill whatever sub-buffers the device has finished with.
static void PumpAudioStream(void) {
    if (!g_player.hasStream || !g_player.playing || !g_player.pcm) return;

    const uint32_t ch = g_player.channels ? g_player.channels : 1;

    while (IsAudioStreamProcessed(g_player.stream)) {
        if (FillStreamChunk(g_pushBuffer, STREAM_CHUNK_FRAMES,
                            g_player.pcm, g_player.frameCount, ch,
                            g_player.loopFrame, g_player.loopEnabled,
                            &g_player.playCursor, &g_player.framesPushed)) {
            g_player.endPushed = true;
        }
        UpdateAudioStream(g_player.stream, g_pushBuffer, (int)STREAM_CHUNK_FRAMES);
    }
}

static void UpdatePlayback(float dt) {
    if (g_player.hasStream) {
        PumpAudioStream();

        if (!g_ui.scrubbing) {
            if (g_player.endPushed) {
                // FillStreamChunk counts only real frames into framesPushed -
                // the silence it pads the last chunk with is not counted - so
                // once the source runs out framesPushed stops growing and
                // StreamFrameAt sticks at frameCount - STREAM_LATENCY_FRAMES,
                // a whole buffer short of the end. What remains is the tail
                // still draining out of the device, so that last stretch runs
                // off the frame clock instead. Paused mid-drain, the readout
                // simply holds rather than snapping back.
                if (g_player.playing) {
                    g_player.position = Clampf(g_player.position + dt, 0.0f, g_player.duration);
                }
            } else {
                g_player.position = StreamPositionSeconds();
            }
        }

        // Advance once that tail has actually been heard, rather than when the
        // last frame was handed over. The old test compared played against
        // frameCount - anchorFrame, which played can never reach for exactly
        // the reason above, so this never fired and the track sat there
        // "playing" with a frozen bar.
        if (g_player.playing && !g_ui.scrubbing && g_player.endPushed &&
            g_player.duration > 0.0f &&
            g_player.position >= g_player.duration - 0.001f) {
            AdvanceOrStop();
        }
    } else if (g_player.playing && !g_ui.scrubbing) {
        // No audio for this slot: keep a clock running so the playlist advances.
        g_player.position += dt;
        if (g_player.position >= g_player.duration) AdvanceOrStop();
    }
}

// ---------------------------------------------------------------------------
// Selection + reordering
// ---------------------------------------------------------------------------

static void SelectOnly(int index) {
    for (int i = 0; i < g_itemCount; i++) g_items[i].selected = (i == index);
}

static void SelectRange(int from, int to) {
    if (from > to) { int t = from; from = to; to = t; }
    for (int i = 0; i < g_itemCount; i++) g_items[i].selected = (i >= from && i <= to);
}

// Move every selected item so the block lands at dropIndex, preserving the
// relative order of the moved items. dropIndex is a gap index in *current*
// list coordinates (0 == before the first row, g_itemCount == after the last),
// which is translated here into "after this many unselected items".
static void MoveSelectionTo(int dropIndex) {
    int selCount = SelectedCount();
    if (selCount == 0 || selCount == g_itemCount) return;
    if (dropIndex < 0) dropIndex = 0;
    if (dropIndex > g_itemCount) dropIndex = g_itemCount;

    int insertAfter = 0;
    for (int i = 0; i < dropIndex; i++) if (!g_items[i].selected) insertAfter++;

    int k = 0, unsel = 0;
    bool inserted = false;
    for (int i = 0; i <= g_itemCount; i++) {
        if (!inserted && unsel == insertAfter) {
            for (int j = 0; j < g_itemCount; j++)
                if (g_items[j].selected) g_scratch[k++] = g_items[j];
            inserted = true;
        }
        if (i == g_itemCount) break;
        if (!g_items[i].selected) { g_scratch[k++] = g_items[i]; unsel++; }
    }

    // Dropping a block back where it started is a no-op, and marking that
    // dirty would make the confirmation dialog appear after an idle drag.
    bool moved = false;
    for (int i = 0; i < k; i++) {
        if (g_scratch[i].uid != g_items[i].uid) { moved = true; break; }
    }

    memcpy(g_items, g_scratch, (size_t)k * sizeof(Item));
    g_itemCount = k;
    if (moved) MarkDirty();
}

static void DeleteSelected(void) {
    // Close the stream first if the row being removed owns the buffer it is
    // reading from, otherwise the free below pulls memory out from under it.
    int playingIdx = IndexOfUid(g_player.uid);
    if (playingIdx >= 0 && g_items[playingIdx].selected) {
        UnloadCurrent();
        g_player.uid = 0;
        g_player.position = 0.0f;
        g_player.duration = 0;
    }

    int k = 0;
    for (int i = 0; i < g_itemCount; i++) {
        if (g_items[i].selected) {
            DropItemPcm(g_items + i);
            BlobRelease(g_items[i].adpcm);
            g_items[i].adpcm = NULL;
            g_items[i].adpcmSize = 0;
        } else {
            g_items[k++] = g_items[i];
        }
    }
    if (k != g_itemCount) MarkDirty();
    g_itemCount = k;
    g_ui.anchorIndex = -1;
    g_ui.lastClickIndex = -1;
}

// The only place loaded audio goes away wholesale: opening another playlist.
static void UnloadPlaylistAudio(void) {
    UnloadCurrent();                       // stream first, then the memory it reads
    g_player.uid = 0;
    g_player.position = 0.0f;
    g_player.duration = 0;
    for (int i = 0; i < g_itemCount; i++) {
        DropItemPcm(g_items + i);
        BlobRelease(g_items[i].adpcm);
        g_items[i].adpcm = NULL;
        g_items[i].adpcmSize = 0;
    }
}

// ---------------------------------------------------------------------------
// Playlist population
// ---------------------------------------------------------------------------

// Append into an explicit array, so the loader thread can build its staging
// list with exactly the same code the main thread uses for g_items.
static Item *AddItemTo(Item *arr, int *count,
                       const char *title, const char *artist, const char *album,
                       int year, float duration, const char *path, const char *format) {
    if (*count >= MAX_ITEMS) return NULL;
    Item *item = &arr[(*count)++];
    memset(item, 0, sizeof(*item));
    snprintf(item->title,  sizeof(item->title),  "%s", title);
    snprintf(item->artist, sizeof(item->artist), "%s", artist);
    snprintf(item->album,  sizeof(item->album),  "%s", album);
    snprintf(item->format, sizeof(item->format), "%s", format);
    if (path) { snprintf(item->path, sizeof(item->path), "%s", path); item->hasFile = true; }
    item->year = year;
    item->duration = duration;
    item->loopPosition = -1;
    item->uid = g_nextUid++;
    return item;
}

static Item *AddItem(const char *title, const char *artist, const char *album,
                     int year, float duration, const char *path, const char *format) {
    return AddItemTo(g_items, &g_itemCount, title, artist, album, year, duration, path, format);
}

// A padding slot. It carries no audio but occupies an index, which is the
// whole point: the archive has to be written back with these in place.
static Item *AddNullTrackTo(Item *arr, int *count) {
    Item *item = AddItemTo(arr, count, "Null track", "", "", 0, 0.0f, NULL, "null");
    if (item) item->kind = TRACK_KIND_NULL;
    return item;
}

// Only the live-playlist variant marks the file dirty. The loader appends to
// g_loadStaging through AddNullTrackTo, and that is a load, not an edit.
static Item *AddNullTrack(void) {
    Item *item = AddNullTrackTo(g_items, &g_itemCount);
    if (item) MarkDirty();
    return item;
}

// Decode any format raylib understands and keep it as 16-bit PCM, matching
// what archive tracks hold, so an imported file is ready to be written back
// out with the rest of them.
static uint8_t *LoadFileAsPcmWav(const char *path, uint32_t *outSize,
                                 uint32_t *outRate, uint8_t *outChannels,
                                 float *outDuration) {
    Wave wave = LoadWave(path);
    if (wave.frameCount == 0) { UnloadWave(wave); return NULL; }

    if (wave.sampleSize != TRACK_BITS_PER_SAMPLE) {
        WaveFormat(&wave, (int)wave.sampleRate, TRACK_BITS_PER_SAMPLE, (int)wave.channels);
    }
    if (!wave.data || wave.sampleSize != TRACK_BITS_PER_SAMPLE) { UnloadWave(wave); return NULL; }

    uint8_t  channels = (uint8_t)(wave.channels ? wave.channels : 1);
    uint32_t rate     = wave.sampleRate ? wave.sampleRate : 44100;
    uint32_t pcmSize  = wave.frameCount * channels * (TRACK_BITS_PER_SAMPLE / 8);

    uint32_t wavSize = 0;
    uint8_t *wav = WrapPcmAsWav(wave.data, pcmSize, rate, channels, &wavSize);
    UnloadWave(wave);                       // the samples were copied into `wav`
    if (!wav) return NULL;

    if (outSize)     *outSize = wavSize;
    if (outRate)     *outRate = rate;
    if (outChannels) *outChannels = channels;
    if (outDuration) *outDuration = (float)((double)(pcmSize / (channels * 2)) / (double)rate);
    return wav;
}

static char *PickAudioFile(const char *title) {
    static const char *patterns[] = { "*.mp3", "*.wav", "*.ogg", "*.flac", "*.qoa" };
    return tinyfd_openFileDialog(title, NULL, 5, patterns, "Audio files", 0);
}

// Point an existing slot at new audio, keeping its index. This is how a null
// slot becomes a real track without disturbing anything around it.
static bool ReplaceTrackAudio(int index, const char *path) {
    if (index < 0 || index >= g_itemCount || !path) return false;

    uint32_t wavSize = 0, rate = 0; uint8_t channels = 0; float duration = 0.0f;
    uint8_t *wav = LoadFileAsPcmWav(path, &wavSize, &rate, &channels, &duration);
    if (!wav) { fprintf(stderr, "Could not decode %s\n", path); return false; }

    Item *item = &g_items[index];

    // If this slot is the one currently streaming, close it before its buffer
    // is freed underneath the stream, then put it back the way it was.
    bool wasLoaded  = (g_player.uid == item->uid);
    bool wasPlaying = wasLoaded && g_player.playing;
    if (wasLoaded) {
        UnloadCurrent();
        g_player.uid = 0;
        g_player.position = 0.0f;
        g_player.duration = 0;
    }

    DropItemPcm(item);
    BlobRelease(item->adpcm);            // the archive's version is gone now
    item->adpcm        = NULL;
    item->adpcmSize    = 0;
    item->interleave   = 0;
    item->pcmSize      = wavSize - 44;
    item->wav          = wav;
    item->wavSize      = wavSize;
    item->sampleRate   = rate;
    item->channels     = channels;
    item->duration     = duration;
    item->loopPosition = -1;
    item->kind         = TRACK_KIND_AUDIO;
    item->hasFile      = false;                 // the samples live here now, not on disk
    snprintf(item->title,  sizeof(item->title),  "%s", GetFileNameWithoutExt(path));
    snprintf(item->format, sizeof(item->format), "%s", "PCM");
    snprintf(item->path,   sizeof(item->path),   "%s", path);   // kept for reference only

    g_pcmHeld += item->wavSize;          // an import is PCM from the start
    item->pcmStamp = g_pcmClock++;

    if (wasLoaded) LoadTrack(index, wasPlaying);
    MarkDirty();
    return true;
}

// Folder import. Decodes to in-memory PCM like everything else, so these
// tracks play through the same streaming path (the Music API is no longer
// used anywhere).
static void LoadFromDirectory(const char *dir) {
    FilePathList files = LoadDirectoryFilesEx(dir, ".mp3;.wav;.ogg;.flac;.qoa;.xm;.mod", false);
    for (unsigned int i = 0; i < files.count && g_itemCount < MAX_ITEMS; i++) {
        const char *path = files.paths[i];
        char name[96];
        snprintf(name, sizeof(name), "%s", GetFileNameWithoutExt(path));
        bool wasDirty = g_dirty;
        if (!AddNullTrack()) break;
        if (!ReplaceTrackAudio(g_itemCount - 1, path)) {
            g_itemCount--;                       // could not decode it, drop the slot
            g_dirty = wasDirty;                  // ...and with it the edit it implied
            continue;
        }
        snprintf(g_items[g_itemCount - 1].title, sizeof(g_items[0].title), "%s", name);
        snprintf(g_items[g_itemCount - 1].album, sizeof(g_items[0].album), "%s", GetFileName(dir));
    }
    UnloadDirectoryFiles(files);
}

// Copy one archive record's samples into a playlist entry. The PCM is copied
// into a RIFF buffer here, so the caller is free to tear the archive arena
// down immediately afterwards.
// Takes a reference on `adpcm` - the caller keeps its own and releases it - so
// several rows addressing the same blob in the .MB end up sharing one buffer.
// `wave` is here for its metadata only; its samples are not kept.
static bool AddArchiveTrackTo(Item *arr, int *count, TwinStudio_Wave wave, const char *album,
                              TwinStudio_StringView* name, uint32_t trackNumber,
                              uint8_t *adpcm, uint32_t adpcmSize, uint32_t interleave,
                              int32_t unkInt) {
    if (!adpcm || adpcmSize == 0) return false;

    uint8_t  channels   = wave.channels ? wave.channels : 1;
    uint32_t sampleRate = wave.samplerate ? wave.samplerate : 44100;

    char title[256];
    if (name == NULL)
    {
        snprintf(title, sizeof(title), "Track %u", trackNumber);
    }
    else
    {
        snprintf(title, sizeof(title), TS_VIEW_FORMAT, TS_VIEW_ARG(*name));
    }

    // No path: nothing about this entry lives on disk.
    Item *item = AddItemTo(arr, count, title, album, album, 0, 0.0f, NULL, "PS2 ADPCM");
    if (!item) return false;

    unsigned long long frames = wave.dataSize / (unsigned long long)(channels * (TRACK_BITS_PER_SAMPLE / 8));

    item->adpcm        = BlobRetain(adpcm);
    item->adpcmSize    = adpcmSize;
    item->interleave   = interleave;
    item->pcmSize      = wave.dataSize;
    item->wav          = NULL;              // decoded when something asks for it
    item->wavSize      = 0;
    item->sampleRate   = sampleRate;
    item->channels     = channels;
    item->loopPosition = wave.loopPosition;
    item->duration     = (float)((double)frames / (double)sampleRate);
    item->unkInt        = unkInt;
    return true;
}

// Open a .MH header plus its data twin and load every track straight into the
// playlist. Nothing is written to disk. Each track's samples are kept in
// memory for as long as the playlist lives, so switching tracks never touches
// the archive again; the previous playlist's audio is released here, and only
// here.
// Runs on the loader thread. Everything here is worker-local: the archive
// arena, the staging playlist, and the WAV buffers it allocates. Ownership of
// those buffers passes to g_items when the main thread publishes.
static TsThreadRet TS_THREAD_CALL ArchiveLoadWorker(void *arg)
{
    (void)arg;

#if USE_RPMALLOC_THREADS
    rpmalloc_thread_initialize();
#endif

    char openedFile[sizeof(g_loader.path)];
    TsMutexLock(&g_loader.mutex);
    snprintf(openedFile, sizeof(openedFile), "%s", g_loader.path);
    TsMutexUnlock(&g_loader.mutex);

    LoaderSetStatus("Reading header", 0, 0);

    TwinRes_MhArchive headerArchive = TwinRes_MhArchiveCreate();

    // The header arena only ever holds the MH's own contents plus two path
    // strings - the records come back through arrput, on the heap. Sizing it
    // from the file instead of a flat 200 MB costs nothing and is one less
    // large block to keep committed for the whole load.
    size_t headerBytes = ClampSize((size_t)GetFileLength(openedFile) * 8u + sizeof(openedFile) * 4u,
                                   HEADER_ARENA_MIN, HEADER_ARENA_MAX);
    TwinStudio_Arena archiveArena = TwinStudio_CreateArena(headerBytes);
    TwinStudio_StringView filePath = TwinStudio_CopyFromCStringArena(&archiveArena, openedFile);
    TwinStudio_BinarySerializer* deserializer = TwinStudio_BinReadFromFile(filePath, false);
    TwinRes_MhArchiveBinDeserialize(NULL, &headerArchive, deserializer, &archiveArena, TwinStudio_BinGetStreamLength(deserializer), NULL);

    LoaderSetStatus("Loading tracks", 0, (int)headerArchive.recordsAmount);

    TwinRes_MbArchive dataArchive = TwinRes_MbArchiveCreate();
    dataArchive.header = headerArchive;
    TwinStudio_StringView mainArchivePath = TwinStudio_CopyFromCStringArena(&archiveArena, openedFile);
    mainArchivePath.dynString[mainArchivePath.length - 1] = 'b';
    if (!FileExists(mainArchivePath.string))
    {
        mainArchivePath.dynString[mainArchivePath.length - 1] = 'B';
    }
    TwinStudio_BinarySerializer* mainArchiveDeserializer = TwinStudio_BinReadFromFile(mainArchivePath, true);
    arrsetcap(dataArchive.items, headerArchive.recordsAmount);

    // A record's ADPCM is lifted straight out of the .MB with stdio: a mono
    // record's bytes start with the 0x30 name block, a stereo record's do not.
    // Nothing decodes at this point, so what a row ends up holding is the
    // archive's own 4-bit data rather than its 16-bit expansion.
    FILE* rawFile = fopen(mainArchivePath.string, "rb");
    if (!rawFile) fprintf(stderr, "Could not reopen %s for record bytes\n", mainArchivePath.string);

    char album[64];
    snprintf(album, sizeof(album), "%s", GetFileNameWithoutExt(openedFile));

    g_loadStagingCount = 0;

    // Reading every record into one arena and only then copying the samples
    // out meant the whole archive existed twice at the peak: once decoded in
    // the arena, once in the per-track RIFF buffers. Reading and copying are
    // interleaved here, and the arena is released every LOAD_ARENA_BUDGET, so
    // what the arena holds no longer scales with the size of the archive.
    size_t peakArena = 0;
    uint64_t adpcmBytes = 0;
    uint64_t sharedBytes = 0;
    uint64_t pcmAvoided = 0;

    // Blobs already decoded, keyed by where they sit in the .MB. A record that
    // addresses one of these takes a reference rather than a second copy.
    typedef struct { uint32_t offset, size; uint8_t *adpcm; uint32_t adpcmSize; } SeenBlob;
    SeenBlob *seen = NULL;
    uint32_t  seenCount = 0;
    if (headerArchive.recordsAmount > 0) {
        seen = (SeenBlob *)calloc(headerArchive.recordsAmount, sizeof *seen);
    }

    uint32_t i = 0;
    while (i < headerArchive.recordsAmount)
    {
        // Take as many records as fit the budget, but never fewer than one:
        // a single record larger than the budget still has to be read.
        size_t   need     = 0;
        uint32_t batchEnd = i;
        while (batchEnd < headerArchive.recordsAmount)
        {
            size_t cost = (size_t)headerArchive.records[batchEnd].size * ADPCM_ARENA_FACTOR
                        + ARENA_PER_RECORD_FAT;
            if (batchEnd > i && need + cost > LOAD_ARENA_BUDGET) break;
            need += cost;
            batchEnd++;
        }

        TwinStudio_Arena batchArena = TwinStudio_CreateArena(need);
        if (need > peakArena) peakArena = need;

        for (; i < batchEnd; ++i)
        {
            TwinRes_MbRecord record = TwinRes_MbArchiveIterateItem(&dataArchive, mainArchiveDeserializer, &batchArena, 0, NULL);

            // Null records become padding slots rather than being dropped. The
            // engine plays tracks by index, so losing one here would renumber
            // everything after it when the archive is written back.
            if (record.header.type == TwinRes_MRT_Null)
            {
                AddNullTrackTo(g_loadStaging, &g_loadStagingCount);
            }
            else
            {
                fprintf(stderr, "Track %d offset %d size %d unkInt %d\n", i + 1, record.header.offset, record.header.size, record.header.unkInt);

                if (record.trackData.loopPosition > 0)
                {
                    fprintf(stderr, "Track %d has a loop point at sample %d\n", i + 1, record.trackData.loopPosition);
                }

                // Same bytes of the .MB as something already loaded? Then it
                // is the same audio, and one buffer serves both rows.
                SeenBlob *hit = NULL;
                for (uint32_t k = 0; k < seenCount && seen; ++k)
                {
                    if (seen[k].offset == record.header.offset && seen[k].size == record.header.size)
                    {
                        hit = seen + k;
                        break;
                    }
                }

                // A mono record carries a 0x30 name block in front of its
                // samples; a stereo one starts at the record's first byte.
                bool     isMono   = (record.header.type == TwinRes_MRT_Mono);
                uint32_t skip     = isMono ? 0x30u : 0u;
                uint32_t rawSize  = (record.header.size > skip) ? record.header.size - skip : 0u;
                uint32_t rawStart = record.header.offset + skip;

                // One reference is held here and released below, so the item
                // taking its own is the same operation whether the blob is
                // fresh or shared.
                uint8_t *adpcm = NULL;
                if (hit)
                {
                    adpcm   = BlobRetain(hit->adpcm);
                    rawSize = hit->adpcmSize;
                }
                else if (rawFile && rawSize > 0)
                {
                    adpcm = BlobAlloc(rawSize);
                    if (adpcm &&
                        (fseek(rawFile, (long)rawStart, SEEK_SET) != 0 ||
                         fread(adpcm, 1, rawSize, rawFile) != rawSize))
                    {
                        fprintf(stderr, "Short read of track %d at 0x%X\n", i + 1, rawStart);
                        BlobRelease(adpcm);
                        adpcm = NULL;
                    }
                }

                TwinStudio_StringView* trackName = isMono ? &record.name : NULL;
                if (AddArchiveTrackTo(g_loadStaging, &g_loadStagingCount, record.trackData, album, trackName, i + 1,
                                      adpcm, rawSize, isMono ? 0u : record.header.interleave,
                                      record.header.unkInt))
                {
                    if (hit)
                    {
                        sharedBytes += rawSize;
                    }
                    else
                    {
                        adpcmBytes += rawSize;
                        if (seen)
                        {
                            seen[seenCount++] = (SeenBlob){ record.header.offset, record.header.size,
                                                            adpcm, rawSize };
                        }
                    }
                    pcmAvoided += record.trackData.dataSize;
                }
                else
                {
                    // Keep the slot so the numbering downstream of it lines up.
                    fprintf(stderr, "Track %d could not be loaded, keeping its slot as padding\n", i + 1);
                    AddNullTrackTo(g_loadStaging, &g_loadStagingCount);
                }
                BlobRelease(adpcm);
            }

            // The samples and the name have been copied into the staging item,
            // so the two fields that point into this arena are cleared before
            // it goes away - the record stays in items[] and must not outlive
            // it holding danglers. Everything that is a plain value is left
            // alone, header and dataSize included, in case IterateItem walks
            // back over what it has already read to place the next record.
            record.trackData.data = NULL;
            record.name = (TwinStudio_StringView){ 0 };
            arrput(dataArchive.items, record);

            LoaderSetProgress((int)i + 1, -1);
        }

        TwinStudio_ArenaFree(&batchArena);
    }

    free(seen);
    if (rawFile) fclose(rawFile);

    fprintf(stderr, "Loaded %d slots into the staging playlist\n", g_loadStagingCount);
    fprintf(stderr, "Audio held: %.1f MB of ADPCM (%.1f MB more saved by sharing repeated blobs); "
                    "%.1f MB of PCM not kept; arena high water %.1f MB\n",
            (double)adpcmBytes / 1048576.0, (double)sharedBytes / 1048576.0,
            (double)pcmAvoided / 1048576.0, (double)peakArena / 1048576.0);

    // Safe to drop the archive now - every track's samples have been copied.
    arrfree(headerArchive.records);
    arrfree(dataArchive.items);
    TwinStudio_ArenaFree(&archiveArena);
    TwinStudio_BinSerializerFree(deserializer);
    TwinStudio_BinSerializerFree(mainArchiveDeserializer);

    LoaderFinish(LOAD_DONE, NULL);

#if USE_RPMALLOC_THREADS
    rpmalloc_thread_finalize();
#endif
    return TS_THREAD_RETURN;
}

static void RunPendingAction(PendingAction action);

// Release anything the worker built but that never made it into g_items.
static void FreeStaging(void)
{
    for (int i = 0; i < g_loadStagingCount; i++) {
        BlobRelease(g_loadStaging[i].wav);
        g_loadStaging[i].wav = NULL;
        BlobRelease(g_loadStaging[i].adpcm);
        g_loadStaging[i].adpcm = NULL;
    }
    g_loadStagingCount = 0;
}

// Main thread. Picks the file (dialogs belong on the UI thread), drops the old
// playlist so peak memory stays bounded, then hands the path to the worker.
void OpenMusicArchive(void)
{
    if (IsLoading()) return;

    const char* filterPatterns[1];
    filterPatterns[0] = "*.MH";
    char* openedFile = tinyfd_openFileDialog("Select MH archive", NULL, 1, filterPatterns, "*.MH Music Header", 0);
    if (openedFile == NULL)
    {
        fprintf(stderr, "No file opened!\n");
        return;
    }

    fprintf(stderr, "Opened %s\n", openedFile);

    // Unloading touches the audio device, so it has to happen here rather than
    // on the worker. It also means the worker is the only owner of track audio
    // while it runs, instead of holding two playlists at once.
    UnloadPlaylistAudio();
    g_itemCount             = 0;
    g_ui.anchorIndex        = -1;
    g_ui.lastClickIndex     = -1;
    g_ui.hoverIndex         = -1;
    g_ui.pendingScrollIndex = -1;
    FreeStaging();

    // The old playlist is gone from here on, so there is nothing left to
    // warn about; whatever the worker publishes starts clean.
    g_dirty = false;

    TsMutexLock(&g_loader.mutex);
    snprintf(g_loader.path, sizeof(g_loader.path), "%s", openedFile);
    snprintf(g_loader.status, sizeof(g_loader.status), "%s", "Opening archive");
    g_loader.error[0] = '\0';
    g_loader.done  = 0;
    g_loader.total = 0;
    g_loader.job   = JOB_OPEN;
    g_loader.state = LOAD_RUNNING;
    TsMutexUnlock(&g_loader.mutex);

    if (!TsThreadStart(&g_loader.thread, ArchiveLoadWorker, NULL)) {
        g_loader.threadValid = false;
        LoaderFinish(LOAD_FAILED, "Could not start the loader thread");
        return;
    }
    g_loader.threadValid = true;
}

// What the confirmation dialog was standing in front of, carried out now that
// the answer is in. Called either straight from the dialog (Discard) or from
// PumpArchiveLoader once a save started from the dialog has landed (Save).
static void RunPendingAction(PendingAction action)
{
    switch (action) {
        case PENDING_OPEN: OpenMusicArchive(); break;
        case PENDING_EXIT: g_quit = true;      break;
        case PENDING_NONE: break;
    }
}

// Called once a frame. Moves a finished load into the live playlist.
static void PumpArchiveLoader(void)
{
    LoadState state = LoaderState();
    if (state != LOAD_DONE && state != LOAD_FAILED) return;

    if (g_loader.threadValid) {
        TsThreadJoin(&g_loader.thread);   // worker has finished; just reap it
        g_loader.threadValid = false;
    }

    JobKind job = CurrentJob();

    if (state == LOAD_DONE && job == JOB_SAVE) {
        fprintf(stderr, "Save finished\n");
        g_dirty = false;                  // what is in memory is now on disk
        TsMutexLock(&g_loader.mutex);
        g_loader.job = JOB_NONE;
        TsMutexUnlock(&g_loader.mutex);
        LoaderFinish(LOAD_IDLE, NULL);

        // "Save" in the confirmation dialog: the thing it was blocking runs
        // here, once the bytes are actually written and not a moment before.
        PendingAction queued = g_ui.afterSaveAction;
        g_ui.afterSaveAction = PENDING_NONE;
        RunPendingAction(queued);
        return;
    }

    if (state == LOAD_DONE) {
        // One copy, between frames: the UI never sees a partial list.
        memcpy(g_items, g_loadStaging, (size_t)g_loadStagingCount * sizeof(Item));
        g_itemCount = g_loadStagingCount;
        g_loadStagingCount = 0;           // ownership of the buffers moved across

        fprintf(stderr, "Published %d slots (%d padding)\n", g_itemCount, NullCount());

        if (g_itemCount > 0) { SelectOnly(0); g_ui.anchorIndex = 0; }

        Clay_ScrollContainerData sd = Clay_GetScrollContainerData(Clay_GetElementId(CLAY_STRING("ListScroll")));
        if (sd.found) sd.scrollPosition->y = 0.0f;
    } else {
        if (job == JOB_OPEN) FreeStaging();
        // A save that failed saved nothing, so whatever was queued behind it
        // stays cancelled rather than quietly discarding the playlist.
        g_ui.afterSaveAction = PENDING_NONE;
        TsMutexLock(&g_loader.mutex);
        fprintf(stderr, "Archive job failed: %s\n", g_loader.error);
        TsMutexUnlock(&g_loader.mutex);
    }

    TsMutexLock(&g_loader.mutex);
    g_loader.job = JOB_NONE;
    TsMutexUnlock(&g_loader.mutex);
    LoaderFinish(LOAD_IDLE, NULL);
}

// ---------------------------------------------------------------------------
// Input (runs before the layout pass, querying last frame's bounding boxes)
// ---------------------------------------------------------------------------

static Clay_ElementId ListScrollId(void) { return Clay_GetElementId(CLAY_STRING("ListScroll")); }

static int ComputeDropIndex(float mouseY) {
    for (int i = 0; i < g_itemCount; i++) {
        Clay_ElementData d = Clay_GetElementData(CLAY_IDI("ListItem", i));
        if (!d.found) continue;
        if (mouseY < d.boundingBox.y + d.boundingBox.height * 0.5f) return i;
    }
    return g_itemCount;
}

static int ItemUnderPointer(Vector2 mouse) {
    Clay_ElementData list = Clay_GetElementData(ListScrollId());
    if (!list.found || !PointInBox(list.boundingBox, mouse)) return -1;
    for (int i = 0; i < g_itemCount; i++) {
        Clay_ElementData d = Clay_GetElementData(CLAY_IDI("ListItem", i));
        if (d.found && PointInBox(d.boundingBox, mouse)) return i;
    }
    return -1;
}

static void ClampScroll(Clay_ScrollContainerData *sd) {
    if (!sd->found) return;
    float maxScroll = sd->contentDimensions.height - sd->scrollContainerDimensions.height;
    if (maxScroll < 0) maxScroll = 0;
    sd->scrollPosition->y = Clampf(sd->scrollPosition->y, -maxScroll, 0.0f);
}

static void HandleScrollbar(Vector2 mouse) {
    Clay_ScrollContainerData sd = Clay_GetScrollContainerData(ListScrollId());
    if (!sd.found) { g_ui.scrollbarDrag = false; return; }

    float trackH   = sd.scrollContainerDimensions.height;
    float contentH = sd.contentDimensions.height;
    if (contentH <= trackH) { g_ui.scrollbarDrag = false; return; }

    float thumbH    = fmaxf((trackH / contentH) * trackH, 30.0f);
    float maxOffset = trackH - thumbH;
    float maxScroll = contentH - trackH;

    if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
        if (Clay_PointerOver(Clay_GetElementId(CLAY_STRING("ScrollThumb")))) {
            g_ui.scrollbarDrag = true;
            g_ui.scrollGrabY   = mouse.y;
            g_ui.scrollOriginY = sd.scrollPosition->y;
        } else if (Clay_PointerOver(Clay_GetElementId(CLAY_STRING("ScrollTrack")))) {
            // jump: centre the thumb on the click, then keep dragging
            Clay_ElementData track = Clay_GetElementData(Clay_GetElementId(CLAY_STRING("ScrollTrack")));
            float local = mouse.y - track.boundingBox.y - thumbH * 0.5f;
            float t = maxOffset > 0 ? Clampf(local / maxOffset, 0.0f, 1.0f) : 0.0f;
            sd.scrollPosition->y = -t * maxScroll;
            g_ui.scrollbarDrag = true;
            g_ui.scrollGrabY   = mouse.y;
            g_ui.scrollOriginY = sd.scrollPosition->y;
        }
    }

    if (g_ui.scrollbarDrag) {
        if (!IsMouseButtonDown(MOUSE_BUTTON_LEFT)) {
            g_ui.scrollbarDrag = false;
        } else if (maxOffset > 0) {
            sd.scrollPosition->y = g_ui.scrollOriginY -
                                   (mouse.y - g_ui.scrollGrabY) * (maxScroll / maxOffset);
            ClampScroll(&sd);
        }
    }
}

static void AutoScrollWhileDragging(Vector2 mouse, float dt) {
    Clay_ScrollContainerData sd = Clay_GetScrollContainerData(ListScrollId());
    Clay_ElementData list = Clay_GetElementData(ListScrollId());
    if (!sd.found || !list.found) return;

    const float edge = 32.0f, speed = 600.0f;
    float top = list.boundingBox.y, bottom = list.boundingBox.y + list.boundingBox.height;

    if (mouse.y < top + edge && mouse.y > top - 80.0f) {
        sd.scrollPosition->y += speed * Clampf((top + edge - mouse.y) / edge, 0.0f, 1.0f) * dt;
    } else if (mouse.y > bottom - edge && mouse.y < bottom + 80.0f) {
        sd.scrollPosition->y -= speed * Clampf((mouse.y - (bottom - edge)) / edge, 0.0f, 1.0f) * dt;
    }
    ClampScroll(&sd);
}

static void HandleWaveform(Vector2 mouse) {
    Clay_ElementData wave = Clay_GetElementData(Clay_GetElementId(CLAY_STRING("Waveform")));
    if (!wave.found) return;

    float inner = wave.boundingBox.width - WAVE_PAD * 2.0f;
    if (inner <= 0) return;

    if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT) && PointInBox(wave.boundingBox, mouse) &&
        g_player.duration > 0) {
        g_ui.scrubbing = true;
    }
    if (g_ui.scrubbing) {
        float t = Clampf((mouse.x - wave.boundingBox.x - WAVE_PAD) / inner, 0.0f, 1.0f);
        g_ui.scrubPosition = t * g_player.duration;
        g_player.position  = g_ui.scrubPosition;   // live preview while dragging
        if (!IsMouseButtonDown(MOUSE_BUTTON_LEFT)) {
            g_ui.scrubbing = false;
            SeekTo(g_ui.scrubPosition);
        }
    }
}

static void HandleVolumeSlider(Vector2 mouse) {
    Clay_ElementData sl = Clay_GetElementData(Clay_GetElementId(CLAY_STRING("VolumeSlider")));
    if (!sl.found) { g_ui.volumeDrag = false; return; }

    bool over = PointInBox(sl.boundingBox, mouse);

    if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT) && over) g_ui.volumeDrag = true;

    if (g_ui.volumeDrag) {
        // Keep tracking once grabbed, even if the pointer leaves the widget.
        if (sl.boundingBox.width > 0) {
            SetVolume(Clampf((mouse.x - sl.boundingBox.x) / sl.boundingBox.width, 0.0f, 1.0f));
        }
        if (!IsMouseButtonDown(MOUSE_BUTTON_LEFT)) g_ui.volumeDrag = false;
    } else if (over) {
        // Nothing here is a scroll container, so the wheel is free to use.
        float wheel = GetMouseWheelMoveV().y;
        if (wheel != 0.0f) SetVolume(g_player.volume + wheel * 0.05f);
    }
}

// ---------------------------------------------------------------------------
// Menu bar actions
//
// Left empty on purpose. AddItem() appends to the global array and
// g_itemCount / g_items are the playlist, so a loader would reset g_itemCount
// to 0 and call AddItem() per entry; a writer would walk g_items[0..count).
// ---------------------------------------------------------------------------

// Put the confirmation dialog in front of an action, or let it straight
// through when there is nothing at stake.
static void RequestAction(PendingAction action) {
    if (IsBusy()) return;                 // a worker owns the playlist
    if (!g_dirty || g_itemCount == 0) { RunPendingAction(action); return; }

    g_ui.confirmOpen    = true;
    g_ui.pendingAction  = action;
    g_ui.aboutOpen      = false;          // one modal at a time
}

static void OnOpenClicked(void) {
    RequestAction(PENDING_OPEN);
}

// Runs on the worker thread. Reads g_items but never changes it; the UI is
// frozen for the duration, so nothing moves underneath it.
static TsThreadRet TS_THREAD_CALL ArchiveSaveWorker(void *arg)
{
    (void)arg;

#if USE_RPMALLOC_THREADS
    rpmalloc_thread_initialize();
#endif

    char headerPath[1024];
    char mainPath[1024];
    TsMutexLock(&g_loader.mutex);
    snprintf(headerPath, sizeof(headerPath), "%s", g_loader.savePathHeader);
    snprintf(mainPath,   sizeof(mainPath),   "%s", g_loader.savePathMain);
    TsMutexUnlock(&g_loader.mutex);

    LoaderSetStatus("Encoding tracks", 0, g_itemCount);

    TwinRes_MhArchive headerArchive = TwinRes_MhArchiveCreate();
    headerArchive.interleave = 65536;
    headerArchive.recordsAmount = g_itemCount;
    arrsetcap(headerArchive.records, g_itemCount);

    TwinRes_MbArchive mainArchive = TwinRes_MbArchiveCreate();
    arrsetcap(mainArchive.items, g_itemCount);

    // Sized from what is actually going to be written rather than a flat
    // 300 MB. Encoding shrinks the data (16-bit PCM in, 4-bit ADPCM out), so
    // half the PCM total plus a fixed margin is comfortably above anything the
    // serializer needs, and the ceiling is the old constant - this can only
    // ask for less than the previous version did, never more.
    uint64_t totalPcm = 0;
    for (int i = 0; i < g_itemCount; i++)
    {
        if (g_items[i].kind != TRACK_KIND_NULL)
        {
            totalPcm += g_items[i].pcmSize;
        }
    }
    size_t writeBytes = ClampSize((size_t)(totalPcm / 2) + 32UL * 1024UL * 1024UL,
                                  WRITE_ARENA_MIN, WRITE_ARENA_MAX);
    fprintf(stderr, "Write arena %.1f MB for %.1f MB of PCM\n",
            (double)writeBytes / 1048576.0, (double)totalPcm / 1048576.0);
    TwinStudio_Arena writeArena = TwinStudio_CreateArena(writeBytes);

    // The serializer is handed the whole items[] array at the end, with every
    // trackData pointer live, so the decodes cannot be released as they go:
    // eviction is off for the duration and the cache is trimmed afterwards.
    g_pcmPinAll = true;

    bool isUndefinedAdded = false;
    TwinRes_MhRecord undefinedRecord = TwinRes_MhRecordCreate();
    uint32_t offset = 0;
    for (uint32_t i = 0; i < (uint32_t)g_itemCount; ++i)
    {
        TwinRes_MhRecord headerRecord = TwinRes_MhRecordCreate();
        TwinRes_MhRecord* writeRecord = &headerRecord;
        Item* item = g_items + i;

        if (item->kind == TRACK_KIND_NULL)
        {
            headerRecord.type = TwinRes_MRT_Null;
            headerRecord.offset = 0;
            headerRecord.size = 0;
            arrput(headerArchive.records, headerRecord);
            continue;
        }

        // undefined moment
        if (strncmp(item->title, "undefined", sizeof("undefined")) == 0)
        {
            if (isUndefinedAdded)
            {
                arrput(headerArchive.records, undefinedRecord);
                continue;
            }

            isUndefinedAdded = true;
            writeRecord = &undefinedRecord;
        }

        // Rows hold ADPCM and decode on demand, so the samples may not exist
        // yet. A row that cannot produce any is written as padding rather than
        // silently shortening the archive and renumbering everything after it.
        if (!EnsureItemPcm(item))
        {
            fprintf(stderr, "Track %d has no samples to write, saving it as padding\n", i + 1);
            headerRecord.type = TwinRes_MRT_Null;
            headerRecord.size = 0;
            headerRecord.offset = 0;
            arrput(headerArchive.records, headerRecord);
            continue;
        }

        writeRecord->sampleRate = item->sampleRate;
        writeRecord->unkInt = item->unkInt;
        writeRecord->type = (item->channels == 2) ? TwinRes_MRT_Stereo : TwinRes_MRT_Mono;
        writeRecord->interleave = (writeRecord->type == TwinRes_MRT_Mono)
                                ? 0 : headerArchive.interleave;

        TwinRes_MbRecord trackRecord = TwinRes_MbRecordCreate();
        trackRecord.interleave = (writeRecord->type == TwinRes_MRT_Mono)
                               ? (item->wavSize - 44) / 4 : headerArchive.interleave / 16;
        trackRecord.trackData = (TwinStudio_Wave) {
            .data = item->wav + 44,
            .dataSize = item->wavSize - 44,
            .samplerate = item->sampleRate,
            .channels = item->channels,
            .loopPosition = -1,
        };

        trackRecord.header = *writeRecord;
        TwinStudio_BinarySerializer* tempSerializer = TwinStudio_BinSerializerAllocate(NULL, TwinStudio_BinarySerializerModeWrite, 0, false);
        // This one only has to survive measuring a single track, so it is
        // sized from that track instead of a flat 20 MB per iteration.
        TwinStudio_Arena tempArena = TwinStudio_CreateArena(
            ClampSize((size_t)(item->wavSize / 2) + 1024UL * 1024UL,
                      TRACK_ARENA_MIN, TRACK_ARENA_MAX));
        TwinStudio_WaveBinSerialize(&trackRecord.trackData, tempSerializer, &tempArena, 0, &trackRecord);
        writeRecord->size = TwinStudio_BinGetStreamPosition(tempSerializer);
        if (writeRecord->type == TwinRes_MRT_Mono)
        {
            writeRecord->size += 0x30;
        }
        TwinStudio_BinSerializerFree(tempSerializer);
        TwinStudio_ArenaFree(&tempArena);

        writeRecord->offset = offset;

        trackRecord.header = *writeRecord;
        if (trackRecord.header.type == TwinRes_MRT_Mono)
        {
            // The name is a fixed 16 byte field in the record, but
            // TwinStudio_CopyFromCStringArena only allocates strlen + 1, so a
            // shorter name left the rest of the field pointing at whatever
            // happened to sit next in the arena - which is the garbage that
            // shows up after the name.
            //
            // Pad to a full 15 characters first so the copy really is 16 bytes
            // long, then put the tail back to zero and report the whole field
            // as the length. Nothing is read past the allocation either way.
            char slice[16];
            memset(slice, '\0', sizeof(slice));

            size_t nameLen = 0;
            while (nameLen < sizeof(slice) - 1 && item->title[nameLen] != '\0') nameLen++;
            memcpy(slice, item->title, nameLen);
            memset(slice + nameLen, ' ', (sizeof(slice) - 1) - nameLen);
            slice[sizeof(slice) - 1] = '\0';

            TwinStudio_StringView nameView = TwinStudio_CopyFromCStringArena(&writeArena, slice);
            memset(nameView.dynString + nameLen, 0, sizeof(slice) - nameLen);
            nameView.length = sizeof(slice);

            trackRecord.name = nameView;
            trackRecord.trackSize = writeRecord->size - 0x30;
            trackRecord.sampleRate = writeRecord->sampleRate;
        }

        arrput(headerArchive.records, *writeRecord);
        arrput(mainArchive.items, trackRecord);

        offset += writeRecord->size;
        offset = (offset + 0x7FFu) & ~0x7FFu;

        LoaderSetProgress((int)i + 1, -1);
    }

    mainArchive.header = headerArchive;

    // No per-record information from here on: total 0 puts the bar into
    // its indeterminate, continuously sliding mode.
    LoaderSetStatus("Writing header", 0, 0);

    FILE* headerFile = fopen(headerPath, "wb");
    if (!headerFile)
    {
        arrfree(mainArchive.items);
        arrfree(headerArchive.records);
        TwinStudio_ArenaFree(&writeArena);
        UnpinPcmCache();
        LoaderFinish(LOAD_FAILED, "Could not open the .MH file for writing");
        return TS_THREAD_RETURN;
    }
    TwinStudio_BinarySerializer* headerWriter = TwinStudio_BinSerializerAllocate(headerFile, TwinStudio_BinarySerializerModeWrite, UINT_MAX, true);
    TwinRes_MhArchiveBinSerialize(&headerArchive, headerWriter, &writeArena, 0, NULL);
    TwinStudio_BinSerializerFree(headerWriter);
    fclose(headerFile);

    LoaderSetStatus("Writing track data", 0, 0);

    FILE* mainFile = fopen(mainPath, "wb");
    if (!mainFile)
    {
        arrfree(mainArchive.items);
        arrfree(headerArchive.records);
        TwinStudio_ArenaFree(&writeArena);
        UnpinPcmCache();
        LoaderFinish(LOAD_FAILED, "Could not open the .MB file for writing");
        return TS_THREAD_RETURN;
    }
    TwinStudio_BinarySerializer* mainWriter = TwinStudio_BinSerializerAllocate(mainFile, TwinStudio_BinarySerializerModeWrite, UINT_MAX, true);
    TwinRes_MbArchiveBinSerialize(&mainArchive, mainWriter, &writeArena, 0, NULL);
    TwinStudio_BinSerializerFree(mainWriter);
    fclose(mainFile);

    arrfree(mainArchive.items);
    arrfree(headerArchive.records);
    TwinStudio_ArenaFree(&writeArena);
    UnpinPcmCache();

    LoaderFinish(LOAD_DONE, NULL);

#if USE_RPMALLOC_THREADS
    rpmalloc_thread_finalize();
#endif
    return TS_THREAD_RETURN;
}

// Main thread. The file dialog belongs here; everything after it is handed to
// the worker so the window keeps drawing.
//
// Returns true once the worker is running. The confirmation dialog needs that
// answer: if the file picker was dismissed, nothing was saved, so it has to
// stay up rather than let the action behind it go ahead.
static bool SaveArchives(void)
{
    if (IsBusy()) return false;

    const char* filterPatterns[1];
    filterPatterns[0] = "*.MH";
    char* savePath = tinyfd_saveFileDialog("Save resulting MH/MB archive", NULL, 1, filterPatterns, "*.MH Music Header");

    if (!savePath)
    {
        fprintf(stderr, "Saving cancelled!\n");
        return false;
    }

    char headerPath[1024];
    char mainPath[1024];
    {
        const char *ext = GetFileExtension(savePath);
        size_t stemLen = (ext && (strcmp(ext, ".MH") == 0 || strcmp(ext, ".mh") == 0))
                       ? (size_t)(ext - savePath) : strlen(savePath);
        if (stemLen >= sizeof(headerPath) - 4) stemLen = sizeof(headerPath) - 5;
        snprintf(headerPath, sizeof(headerPath), "%.*s.MH", (int)stemLen, savePath);
        snprintf(mainPath,   sizeof(mainPath),   "%.*s.MB", (int)stemLen, savePath);
    }
    fprintf(stderr, "Save path %s + %s\n", headerPath, mainPath);

    TsMutexLock(&g_loader.mutex);
    snprintf(g_loader.savePathHeader, sizeof(g_loader.savePathHeader), "%s", headerPath);
    snprintf(g_loader.savePathMain,   sizeof(g_loader.savePathMain),   "%s", mainPath);
    snprintf(g_loader.status, sizeof(g_loader.status), "%s", "Preparing");
    g_loader.error[0] = '\0';
    g_loader.done  = 0;
    g_loader.total = g_itemCount;
    g_loader.job   = JOB_SAVE;
    g_loader.state = LOAD_RUNNING;
    TsMutexUnlock(&g_loader.mutex);

    if (!TsThreadStart(&g_loader.thread, ArchiveSaveWorker, NULL)) {
        g_loader.threadValid = false;
        LoaderFinish(LOAD_FAILED, "Could not start the save thread");
        return false;
    }
    g_loader.threadValid = true;
    return true;
}

static void OnSaveClicked(void) {
    SaveArchives();
}

// Select a freshly appended row and ask for it to be scrolled to once it has
// been through a layout pass (its box does not exist until then).
static void FocusNewRow(void) {
    if (g_itemCount <= 0) return;
    int idx = g_itemCount - 1;
    SelectOnly(idx);
    g_ui.anchorIndex = idx;
    g_ui.lastClickIndex = -1;
    g_ui.pendingScrollIndex = idx;
}

static void OnAddTrackClicked(void) {
    if (g_itemCount >= MAX_ITEMS) { fprintf(stderr, "Playlist is full\n"); return; }

    char *path = PickAudioFile("Add a track");
    if (!path) return;

    bool wasDirty = g_dirty;
    Item *item = AddNullTrack();                 // take the slot, then fill it
    if (!item) return;
    if (!ReplaceTrackAudio(g_itemCount - 1, path)) {
        g_itemCount--;                           // decode failed, drop the empty slot again
        g_dirty = wasDirty;                      // nothing changed, so nothing to save
        return;
    }
    FocusNewRow();
}

static void OnAddNullClicked(void) {
    if (g_itemCount >= MAX_ITEMS) { fprintf(stderr, "Playlist is full\n"); return; }
    if (AddNullTrack()) FocusNewRow();
}

static void OnRemoveClicked(void) {
    DeleteSelected();
}

// The item already holds a complete RIFF file, so exporting is a byte dump -
// no re-encode, and what lands on disk is exactly what is being played.
static void OnExportClicked(void) {
    int idx = FirstSelected();
    if (idx < 0 || SelectedCount() != 1) return;

    Item *item = &g_items[idx];
    if (!ItemHasAudio(item) || !EnsureItemPcm(item)) return;

    char suggested[160];
    snprintf(suggested, sizeof(suggested), "%s.wav",
             item->title[0] ? item->title : "track");

    static const char *patterns[] = { "*.wav" };
    char *path = tinyfd_saveFileDialog("Export track to WAV", suggested, 1, patterns, "WAV audio");
    if (!path) return;

    // tinyfd does not always append the extension the filter asked for.
    char finalPath[1024];
    const char *ext = GetFileExtension(path);
    if (ext && (strcmp(ext, ".wav") == 0 || strcmp(ext, ".WAV") == 0)) {
        snprintf(finalPath, sizeof(finalPath), "%s", path);
    } else {
        snprintf(finalPath, sizeof(finalPath), "%s.wav", path);
    }

    FILE *f = fopen(finalPath, "wb");
    if (!f) { fprintf(stderr, "Could not open %s for writing\n", finalPath); return; }
    size_t written = fwrite(item->wav, 1, item->wavSize, f);
    fclose(f);

    if (written != item->wavSize) fprintf(stderr, "Short write exporting %s\n", finalPath);
    else                          fprintf(stderr, "Exported %s\n", finalPath);
}

static void OnReplaceClicked(void) {
    int idx = FirstSelected();
    if (idx < 0 || SelectedCount() != 1) return;

    char *path = PickAudioFile("Replace track audio");
    if (!path) return;
    ReplaceTrackAudio(idx, path);
}

// The dialog is modal, so this is the only handler that runs while it is open.
// Closes on the button, on Esc, or on a click anywhere outside the card.
static void HandleAboutDialog(Vector2 mouse) {
    if (!g_ui.aboutOpen) return;

    if (IsKeyPressed(KEY_ESCAPE)) { g_ui.aboutOpen = false; return; }
    if (!IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) return;

    if (Clay_PointerOver(Clay_GetElementId(CLAY_STRING("AboutCloseButton")))) {
        g_ui.aboutOpen = false;
        return;
    }

    // Anywhere off the card dismisses it. Uses the box rather than
    // Clay_PointerOver so a click on the scrim cannot be mistaken for a hit.
    Clay_ElementData card = Clay_GetElementData(Clay_GetElementId(CLAY_STRING("AboutDialog")));
    if (card.found && !PointInBox(card.boundingBox, mouse)) g_ui.aboutOpen = false;
}

// Also modal, and it outranks the About dialog: nothing else runs while it is
// up. Cancel is the safe answer, so Esc and a click off the card both pick it.
static void HandleConfirmDialog(Vector2 mouse) {
    if (!g_ui.confirmOpen) return;

    PendingAction action = g_ui.pendingAction;

    if (IsKeyPressed(KEY_ESCAPE)) {
        g_ui.confirmOpen = false;
        g_ui.pendingAction = PENDING_NONE;
        return;
    }
    if (!IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) return;

    if (Clay_PointerOver(Clay_GetElementId(CLAY_STRING("ConfirmSaveButton")))) {
        // The dialog stays up if the file picker was dismissed - nothing was
        // written, so the action behind it must not go ahead.
        if (!SaveArchives()) return;
        g_ui.confirmOpen     = false;
        g_ui.pendingAction   = PENDING_NONE;
        g_ui.afterSaveAction = action;    // resumed by PumpArchiveLoader
        return;
    }

    if (Clay_PointerOver(Clay_GetElementId(CLAY_STRING("ConfirmDiscardButton")))) {
        g_ui.confirmOpen   = false;
        g_ui.pendingAction = PENDING_NONE;
        RunPendingAction(action);
        return;
    }

    if (Clay_PointerOver(Clay_GetElementId(CLAY_STRING("ConfirmCancelButton")))) {
        g_ui.confirmOpen   = false;
        g_ui.pendingAction = PENDING_NONE;
        return;
    }

    // Off the card cancels. Uses the box rather than Clay_PointerOver so a
    // click on the scrim cannot be mistaken for a hit on the card.
    Clay_ElementData card = Clay_GetElementData(Clay_GetElementId(CLAY_STRING("ConfirmDialog")));
    if (card.found && !PointInBox(card.boundingBox, mouse)) {
        g_ui.confirmOpen   = false;
        g_ui.pendingAction = PENDING_NONE;
    }
}

static void HandleMenuBar(void) {
    if (!IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) return;
    if (IsBusy()) return;         // a background job owns the playlist
    if (Clay_PointerOver(Clay_GetElementId(CLAY_STRING("OpenButton")))) OnOpenClicked();
    else if (Clay_PointerOver(Clay_GetElementId(CLAY_STRING("SaveButton")))) { if (g_itemCount > 0) OnSaveClicked(); }
    else if (Clay_PointerOver(Clay_GetElementId(CLAY_STRING("AddTrackButton")))) OnAddTrackClicked();
    else if (Clay_PointerOver(Clay_GetElementId(CLAY_STRING("AddNullButton")))) OnAddNullClicked();
    else if (Clay_PointerOver(Clay_GetElementId(CLAY_STRING("RemoveButton")))) {
        if (SelectedCount() > 0) OnRemoveClicked();
    }
    else if (Clay_PointerOver(Clay_GetElementId(CLAY_STRING("ReplaceButton")))) OnReplaceClicked();
    else if (Clay_PointerOver(Clay_GetElementId(CLAY_STRING("ExportButton")))) OnExportClicked();
    else if (Clay_PointerOver(Clay_GetElementId(CLAY_STRING("AboutButton")))) g_ui.aboutOpen = true;
}

static void HandleTransportButtons(void) {
    if (!IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) return;
    if (Clay_PointerOver(Clay_GetElementId(CLAY_STRING("PlayButton")))) TogglePlay();
    else if (Clay_PointerOver(Clay_GetElementId(CLAY_STRING("PrevButton")))) Skip(-1);
    else if (Clay_PointerOver(Clay_GetElementId(CLAY_STRING("NextButton")))) Skip(1);
    else if (Clay_PointerOver(Clay_GetElementId(CLAY_STRING("LoopButton")))) SetLoopWanted(!g_player.loopWanted);
}

static void HandleListInput(Vector2 mouse, float dt) {
    g_ui.hoverIndex = (g_ui.dragging || g_ui.scrollbarDrag) ? -1 : ItemUnderPointer(mouse);

    // ---- press ----
    if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT) && !g_ui.scrollbarDrag) {
        int idx = ItemUnderPointer(mouse);
        if (idx >= 0) {
            double now = GetTime();
            bool doubleClick = (now - g_ui.lastClickTime < 0.35) && g_ui.lastClickIndex == idx;
            g_ui.lastClickTime = now;
            g_ui.lastClickIndex = idx;

            if (doubleClick) {
                SelectOnly(idx);
                g_ui.anchorIndex = idx;
                LoadTrack(idx, true);
            } else {
                g_ui.pressedInList    = true;
                g_ui.dragCandidate    = true;
                g_ui.dragging         = false;
                g_ui.pressIndex       = idx;
                g_ui.pressPos         = mouse;
                g_ui.pressWasSelected = g_items[idx].selected;

                if (ShiftDown() && g_ui.anchorIndex >= 0 && g_ui.anchorIndex < g_itemCount) {
                    SelectRange(g_ui.anchorIndex, idx);
                } else if (CtrlDown()) {
                    g_items[idx].selected = !g_items[idx].selected;
                    g_ui.anchorIndex = idx;
                    g_ui.dragCandidate = g_items[idx].selected;
                } else if (!g_items[idx].selected) {
                    SelectOnly(idx);
                    g_ui.anchorIndex = idx;
                }
                // A plain click on an already selected row is resolved on
                // release, so dragging a multi-selection keeps the whole block.
            }
        } else {
            Clay_ElementData list = Clay_GetElementData(ListScrollId());
            if (list.found && PointInBox(list.boundingBox, mouse) && !CtrlDown() && !ShiftDown()) {
                for (int i = 0; i < g_itemCount; i++) g_items[i].selected = false;
                g_ui.anchorIndex = -1;
            }
        }
    }

    // ---- drag ----
    if (g_ui.dragCandidate && IsMouseButtonDown(MOUSE_BUTTON_LEFT) && !g_ui.dragging) {
        float dx = mouse.x - g_ui.pressPos.x, dy = mouse.y - g_ui.pressPos.y;
        if (dx * dx + dy * dy > 25.0f) {                   // 5px threshold
            g_ui.dragging = true;
            if (g_ui.pressIndex >= 0 && g_ui.pressIndex < g_itemCount &&
                !g_items[g_ui.pressIndex].selected) {
                SelectOnly(g_ui.pressIndex);
                g_ui.anchorIndex = g_ui.pressIndex;
            }
        }
    }

    if (g_ui.dragging) {
        g_ui.dropIndex = ComputeDropIndex(mouse.y);
        AutoScrollWhileDragging(mouse, dt);
    } else {
        g_ui.dropIndex = -1;
    }

    // ---- release ----
    if (IsMouseButtonReleased(MOUSE_BUTTON_LEFT)) {
        if (g_ui.dragging) {
            MoveSelectionTo(g_ui.dropIndex);
            g_ui.anchorIndex = FirstSelected();
            g_ui.lastClickIndex = -1;          // indices moved, don't fake a double click
        } else if (g_ui.pressedInList && g_ui.pressWasSelected && !CtrlDown() && !ShiftDown()) {
            int idx = ItemUnderPointer(mouse);
            if (idx >= 0 && idx == g_ui.pressIndex) {
                SelectOnly(idx);
                g_ui.anchorIndex = idx;
            }
        }
        g_ui.dragging      = false;
        g_ui.dragCandidate = false;
        g_ui.pressedInList = false;
        g_ui.dropIndex     = -1;
    }
}

static void ScrollRowIntoView(int index) {
    Clay_ScrollContainerData sd = Clay_GetScrollContainerData(ListScrollId());
    Clay_ElementData list = Clay_GetElementData(ListScrollId());
    Clay_ElementData row  = Clay_GetElementData(CLAY_IDI("ListItem", index));
    if (!sd.found || !list.found || !row.found) return;

    float top    = row.boundingBox.y - list.boundingBox.y;
    float bottom = top + row.boundingBox.height;
    if (top < 0) sd.scrollPosition->y -= top;
    else if (bottom > list.boundingBox.height) sd.scrollPosition->y -= bottom - list.boundingBox.height;
    ClampScroll(&sd);
}

static void HandleKeyboard(void) {
    if (IsKeyPressed(KEY_SPACE)) TogglePlay();
    if (IsKeyPressed(KEY_ENTER) || IsKeyPressed(KEY_KP_ENTER)) {
        int idx = FirstSelected();
        if (idx >= 0) LoadTrack(idx, true);
    }
    if (CtrlDown() && IsKeyPressed(KEY_A)) {
        for (int i = 0; i < g_itemCount; i++) g_items[i].selected = true;
        g_ui.anchorIndex = 0;
    }
    if (IsKeyPressed(KEY_DELETE) || IsKeyPressed(KEY_BACKSPACE)) DeleteSelected();

    int move = 0;
    if (IsKeyPressed(KEY_DOWN)) move = 1;
    if (IsKeyPressed(KEY_UP))   move = -1;
    if (move != 0 && g_itemCount > 0) {
        int cur = (move > 0) ? LastSelected() : FirstSelected();
        int next = (cur < 0) ? 0 : cur + move;
        next = (int)Clampf((float)next, 0.0f, (float)(g_itemCount - 1));

        if (ShiftDown() && g_ui.anchorIndex >= 0 && g_ui.anchorIndex < g_itemCount) {
            SelectRange(g_ui.anchorIndex, next);
        } else {
            SelectOnly(next);
            g_ui.anchorIndex = next;
        }
        ScrollRowIntoView(next);
    }
}

// ---------------------------------------------------------------------------
// Layout
// ---------------------------------------------------------------------------

static Clay_TextElementConfig TextCfg(uint16_t size, Clay_Color color, uint16_t fontId) {
    return (Clay_TextElementConfig){ .fontId = fontId, .fontSize = size, .textColor = color };
}

static Clay_TextElementConfig TextCfgNoWrap(uint16_t size, Clay_Color color, uint16_t fontId) {
    return (Clay_TextElementConfig){ .fontId = fontId, .fontSize = size, .textColor = color,
                                     .wrapMode = CLAY_TEXT_WRAP_NONE };
}

static void RenderTransportButton(Clay_ElementId id, Clay_String label, bool primary) {
    // Clay_Hovered() reports the currently *open* element, so out here it would
    // answer for the parent row. The id is known, so ask about it directly.
    bool hovered = Clay_PointerOver(id);
    float size = primary ? 56.0f : 42.0f;
    CLAY(id, {
        .layout = {
            .sizing = { .width = CLAY_SIZING_FIXED(size), .height = CLAY_SIZING_FIXED(size) },
            .childAlignment = { .x = CLAY_ALIGN_X_CENTER, .y = CLAY_ALIGN_Y_CENTER }
        },
        .backgroundColor = primary ? (hovered ? (Clay_Color){120, 190, 255, 255} : C_ACCENT)
                                   : (hovered ? C_HOVER : C_PANEL_2),
        .cornerRadius = CLAY_CORNER_RADIUS(size / 2.0f)
    }) {
        CLAY_TEXT(label, CLAY_TEXT_CONFIG(TextCfg(primary ? 17 : 14,
                        primary ? (Clay_Color){12, 18, 28, 255} : C_TEXT, FONT_BODY)));
    }
}

// Folder: a small tab sitting on top of the body.
static void IconFolder(Clay_Color color) {
    CLAY(CLAY_ID_LOCAL("IconFolder"), {
        .layout = {
            .layoutDirection = CLAY_TOP_TO_BOTTOM,
            .sizing = { .width = CLAY_SIZING_FIXED(18), .height = CLAY_SIZING_FIXED(15) }
        }
    }) {
        CLAY(CLAY_ID_LOCAL("Tab"), {
            .layout = { .sizing = { .width = CLAY_SIZING_FIXED(8), .height = CLAY_SIZING_FIXED(3) } },
            .backgroundColor = color
        }) {}
        CLAY(CLAY_ID_LOCAL("Body"), {
            .layout = { .sizing = { .width = CLAY_SIZING_FIXED(18), .height = CLAY_SIZING_FIXED(12) } },
            .backgroundColor = color,
            .cornerRadius = CLAY_CORNER_RADIUS(2)
        }) {}
    }
}

// Floppy disk: shutter on top, label below, both punched out of the body.
static void IconDisk(Clay_Color color, Clay_Color cutout) {
    CLAY(CLAY_ID_LOCAL("IconDisk"), {
        .layout = {
            .layoutDirection = CLAY_TOP_TO_BOTTOM,
            .sizing = { .width = CLAY_SIZING_FIXED(16), .height = CLAY_SIZING_FIXED(16) },
            .padding = { .top = 2 },
            .childGap = 2,
            .childAlignment = { .x = CLAY_ALIGN_X_CENTER }
        },
        .backgroundColor = color,
        .cornerRadius = CLAY_CORNER_RADIUS(2)
    }) {
        CLAY(CLAY_ID_LOCAL("Shutter"), {
            .layout = { .sizing = { .width = CLAY_SIZING_FIXED(7), .height = CLAY_SIZING_FIXED(5) } },
            .backgroundColor = cutout
        }) {}
        CLAY(CLAY_ID_LOCAL("Label"), {
            .layout = { .sizing = { .width = CLAY_SIZING_FIXED(11), .height = CLAY_SIZING_FIXED(5) } },
            .backgroundColor = cutout
        }) {}
    }
}

// Plus: three centred rows, so the bar and the stem line up without overlap.
static void IconPlus(Clay_Color color) {
    CLAY(CLAY_ID_LOCAL("IconPlus"), {
        .layout = {
            .layoutDirection = CLAY_TOP_TO_BOTTOM,
            .sizing = { .width = CLAY_SIZING_FIXED(13), .height = CLAY_SIZING_FIXED(13) },
            .childAlignment = { .x = CLAY_ALIGN_X_CENTER }
        }
    }) {
        CLAY(CLAY_ID_LOCAL("Top"), {
            .layout = { .sizing = { .width = CLAY_SIZING_FIXED(3), .height = CLAY_SIZING_FIXED(5) } },
            .backgroundColor = color
        }) {}
        CLAY(CLAY_ID_LOCAL("Bar"), {
            .layout = { .sizing = { .width = CLAY_SIZING_FIXED(13), .height = CLAY_SIZING_FIXED(3) } },
            .backgroundColor = color
        }) {}
        CLAY(CLAY_ID_LOCAL("Bottom"), {
            .layout = { .sizing = { .width = CLAY_SIZING_FIXED(3), .height = CLAY_SIZING_FIXED(5) } },
            .backgroundColor = color
        }) {}
    }
}

// Empty slot: an outline with nothing in it. Borders draw inset, so no
// background is needed to get a hollow square.
static void IconNull(Clay_Color color) {
    CLAY(CLAY_ID_LOCAL("IconNull"), {
        .layout = { .sizing = { .width = CLAY_SIZING_FIXED(13), .height = CLAY_SIZING_FIXED(13) } },
        .cornerRadius = CLAY_CORNER_RADIUS(2),
        .border = { .width = CLAY_BORDER_OUTSIDE(2), .color = color }
    }) {}
}

// Bin: handle, lid, body.
static void IconTrash(Clay_Color color) {
    CLAY(CLAY_ID_LOCAL("IconTrash"), {
        .layout = {
            .layoutDirection = CLAY_TOP_TO_BOTTOM,
            .sizing = { .width = CLAY_SIZING_FIXED(14), .height = CLAY_SIZING_FIXED(15) },
            .childGap = 1,
            .childAlignment = { .x = CLAY_ALIGN_X_CENTER }
        }
    }) {
        CLAY(CLAY_ID_LOCAL("Handle"), {
            .layout = { .sizing = { .width = CLAY_SIZING_FIXED(6), .height = CLAY_SIZING_FIXED(2) } },
            .backgroundColor = color
        }) {}
        CLAY(CLAY_ID_LOCAL("Lid"), {
            .layout = { .sizing = { .width = CLAY_SIZING_FIXED(14), .height = CLAY_SIZING_FIXED(2) } },
            .backgroundColor = color
        }) {}
        CLAY(CLAY_ID_LOCAL("Body"), {
            .layout = { .sizing = { .width = CLAY_SIZING_FIXED(10), .height = CLAY_SIZING_FIXED(9) } },
            .backgroundColor = color,
            .cornerRadius = { .bottomLeft = 2, .bottomRight = 2 }
        }) {}
    }
}

// Speaker, drawn with layout elements like the menu bar icons: a small body,
// a cone stepped out of three slices, then arcs that appear as volume rises.
static void IconSpeaker(Clay_Color color, int arcs) {
    CLAY(CLAY_ID_LOCAL("IconSpeaker"), {
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

static void RenderVolumeControl(void) {
    float t = Clampf(g_player.volume, 0.0f, 1.0f);
    bool  hot = g_ui.volumeDrag ||
                Clay_PointerOver(Clay_GetElementId(CLAY_STRING("VolumeSlider")));

    int arcs = (g_player.volume <= 0.001f) ? 0 : (g_player.volume < 0.5f ? 1 : 2);
    Clay_Color iconColor = hot ? C_ACCENT : C_TEXT_DIM;

    CLAY(CLAY_ID("VolumeControl"), {
        .layout = { .childGap = 8, .childAlignment = { .y = CLAY_ALIGN_Y_CENTER } }
    }) {
        IconSpeaker(iconColor, arcs);

        // The hit area is the full height so the slider is easy to grab; the
        // visible track is the thin bar centred inside it.
        CLAY(CLAY_ID("VolumeSlider"), {
            .layout = {
                .sizing = { .width = CLAY_SIZING_FIXED(VOL_W), .height = CLAY_SIZING_FIXED(20) },
                .childAlignment = { .y = CLAY_ALIGN_Y_CENTER }
            }
        }) {
            CLAY(CLAY_ID("VolumeTrack"), {
                .layout = {
                    .sizing = { .width = CLAY_SIZING_GROW(0), .height = CLAY_SIZING_FIXED(4) },
                    .childAlignment = { .y = CLAY_ALIGN_Y_CENTER }
                },
                .backgroundColor = C_WAVE,
                .cornerRadius = CLAY_CORNER_RADIUS(2)
            }) {
                CLAY(CLAY_ID("VolumeFill"), {
                    .layout = { .sizing = { .width = CLAY_SIZING_FIXED(t * VOL_W),
                                            .height = CLAY_SIZING_FIXED(4) } },
                    .backgroundColor = hot ? (Clay_Color){140, 200, 255, 255} : C_ACCENT,
                    .cornerRadius = CLAY_CORNER_RADIUS(2)
                }) {}
            }

            // Floating, so moving the knob never reflows the track.
            CLAY(CLAY_ID("VolumeKnob"), {
                .layout = { .sizing = { .width = CLAY_SIZING_FIXED(12),
                                        .height = CLAY_SIZING_FIXED(12) } },
                .floating = {
                    .offset = { .x = t * VOL_W - 6.0f, .y = 0 },
                    .zIndex = 3,
                    .attachPoints = { .element = CLAY_ATTACH_POINT_LEFT_CENTER,
                                      .parent  = CLAY_ATTACH_POINT_LEFT_CENTER },
                    .pointerCaptureMode = CLAY_POINTER_CAPTURE_MODE_PASSTHROUGH,
                    .attachTo = CLAY_ATTACH_TO_PARENT
                },
                .backgroundColor = hot ? (Clay_Color){255, 255, 255, 255} : C_TEXT,
                .cornerRadius = CLAY_CORNER_RADIUS(6)
            }) {}
        }

        CLAY(CLAY_ID("VolumeReadout"), {
            .layout = { .sizing = { .width = CLAY_SIZING_FIXED(38) },
                        .childAlignment = { .x = CLAY_ALIGN_X_RIGHT } }
        }) {
            CLAY_TEXT(Fmt("%d%%", (int)(t * 100.0f + 0.5f)),
                      CLAY_TEXT_CONFIG(TextCfgNoWrap(13, hot ? C_TEXT : C_TEXT_FAINT, FONT_BODY)));
        }
    }
}

static void RenderWaveform(void) {
    // Bar count follows the element width measured last frame; a one frame lag
    // on resize is invisible and it keeps this a single pass.
    Clay_ElementData prev = Clay_GetElementData(Clay_GetElementId(CLAY_STRING("Waveform")));
    float innerW = prev.found ? prev.boundingBox.width - WAVE_PAD * 2.0f : 600.0f;
    if (innerW < 40.0f) innerW = 40.0f;

    int barCount = (int)(innerW / 5.0f);
    if (barCount < 8)   barCount = 8;
    if (barCount > 400) barCount = 400;

    float progress = (g_player.duration > 0.0f)
                   ? Clampf(g_player.position / g_player.duration, 0.0f, 1.0f) : 0.0f;
    int   playedBars = (int)(progress * barCount);
    float barMaxH = WAVE_H - WAVE_PAD * 2.0f;

    bool hasTrack = g_player.uid != 0;
    bool hovered  = Clay_PointerOver(Clay_GetElementId(CLAY_STRING("Waveform")));

    // Where the loop restarts, in bar units and as a fraction of the track.
    float loopT   = 0.0f;
    int   loopBar = -1;
    if (hasTrack && g_player.loopEnabled && g_player.frameCount > 0 && g_player.loopFrame > 0) {
        loopT   = Clampf((float)g_player.loopFrame / (float)g_player.frameCount, 0.0f, 1.0f);
        loopBar = (int)(loopT * barCount);
    }

    CLAY(CLAY_ID("Waveform"), {
        .layout = {
            .sizing = { .width = CLAY_SIZING_GROW(0), .height = CLAY_SIZING_FIXED(WAVE_H) },
            .padding = { .left = (uint16_t)WAVE_PAD, .right = (uint16_t)WAVE_PAD,
                         .top = (uint16_t)WAVE_PAD, .bottom = (uint16_t)WAVE_PAD },
            .childGap = 2,
            .childAlignment = { .y = CLAY_ALIGN_Y_CENTER }
        },
        .backgroundColor = C_PANEL_2,
        .cornerRadius = CLAY_CORNER_RADIUS(10)
    }) {
        for (int b = 0; b < barCount; b++) {
            float peak = 0.06f;
            if (hasTrack && g_player.peakCount > 0) {
                int from = (int)((float)b       / barCount * g_player.peakCount);
                int to   = (int)((float)(b + 1) / barCount * g_player.peakCount);
                if (to <= from) to = from + 1;
                if (to > g_player.peakCount) to = g_player.peakCount;
                float maxV = 0.0f;
                for (int k = from; k < to; k++) if (g_player.peaks[k] > maxV) maxV = g_player.peaks[k];
                peak = Clampf(maxV, 0.04f, 1.0f);
            }
            bool played = b < playedBars;
            Clay_Color barColor = played ? (g_ui.scrubbing ? (Clay_Color){140, 200, 255, 255} : C_ACCENT)
                                         : (hovered ? (Clay_Color){78, 86, 106, 255} : C_WAVE);

            // Tint the part of the track that repeats, so the loop region is
            // visible without needing to read the sample number.
            if (loopBar >= 0 && b >= loopBar && !played) {
                barColor = hovered ? (Clay_Color){108, 116, 140, 255} : (Clay_Color){86, 94, 116, 255};
            }
            CLAY(CLAY_IDI("WaveBar", b), {
                .layout = { .sizing = { .width = CLAY_SIZING_GROW(0),
                                        .height = CLAY_SIZING_FIXED(peak * barMaxH) } },
                .backgroundColor = barColor,
                .cornerRadius = CLAY_CORNER_RADIUS(1)
            }) {}
        }

        // Loop marker, drawn the same floating way as the playhead.
        if (loopBar >= 0) {
            CLAY(CLAY_ID("LoopMarker"), {
                .layout = { .sizing = { .width = CLAY_SIZING_FIXED(2),
                                        .height = CLAY_SIZING_FIXED(WAVE_H) } },
                .floating = {
                    .offset = { .x = WAVE_PAD + loopT * innerW - 1.0f, .y = 0 },
                    .zIndex = 3,
                    .attachPoints = { .element = CLAY_ATTACH_POINT_LEFT_TOP,
                                      .parent  = CLAY_ATTACH_POINT_LEFT_TOP },
                    .pointerCaptureMode = CLAY_POINTER_CAPTURE_MODE_PASSTHROUGH,
                    .attachTo = CLAY_ATTACH_TO_PARENT
                },
                .backgroundColor = (Clay_Color){255, 196, 92, 220}
            }) {}
        }

        // playhead - floating so it can sit anywhere along the bars without
        // taking part in the layout
        if (hasTrack) {
            CLAY(CLAY_ID("Playhead"), {
                .layout = { .sizing = { .width = CLAY_SIZING_FIXED(2),
                                        .height = CLAY_SIZING_FIXED(WAVE_H) } },
                .floating = {
                    .offset = { .x = WAVE_PAD + progress * innerW - 1.0f, .y = 0 },
                    .zIndex = 4,
                    .attachPoints = { .element = CLAY_ATTACH_POINT_LEFT_TOP,
                                      .parent  = CLAY_ATTACH_POINT_LEFT_TOP },
                    .pointerCaptureMode = CLAY_POINTER_CAPTURE_MODE_PASSTHROUGH,
                    .attachTo = CLAY_ATTACH_TO_PARENT
                },
                .backgroundColor = (Clay_Color){255, 255, 255, 235},
                .cornerRadius = CLAY_CORNER_RADIUS(1)
            }) {}
        }
    }
}

static void RenderPlayerBar(void) {
    int    idx   = IndexOfUid(g_player.uid);
    Item  *track = (idx >= 0) ? &g_items[idx] : NULL;

    Clay_String posStr    = TimeStr(g_player.position);
    Clay_String durStr    = TimeStr(g_player.duration);
    Clay_String remainStr = TimeStr(g_player.duration - g_player.position);

    CLAY(CLAY_ID("PlayerBar"), {
        .layout = {
            .layoutDirection = CLAY_TOP_TO_BOTTOM,
            .sizing = { .width = CLAY_SIZING_GROW(0) },
            .padding = { .left = 24, .right = 24, .top = 18, .bottom = 20 },
            .childGap = 14
        },
        .backgroundColor = C_PANEL,
        .border = { .width = { .top = 1 }, .color = C_LINE }
    }) {
        // transport row
        CLAY(CLAY_ID("TransportRow"), {
            .layout = {
                .sizing = { .width = CLAY_SIZING_GROW(0) },
                .childGap = 12,
                .childAlignment = { .y = CLAY_ALIGN_Y_CENTER }
            }
        }) {
            RenderTransportButton(CLAY_ID("PrevButton"), CLAY_STRING("<<"), false);
            RenderTransportButton(CLAY_ID("PlayButton"),
                                  g_player.playing ? CLAY_STRING("||") : CLAY_STRING(">"), true);
            RenderTransportButton(CLAY_ID("NextButton"), CLAY_STRING(">>"), false);

            // Loop toggle. Only meaningful for a track that carries a loop
            // point; greyed out otherwise so it is obvious which ones do.
            {
                int   loadedIdx = IndexOfUid(g_player.uid);
                Item *loaded    = (loadedIdx >= 0) ? &g_items[loadedIdx] : NULL;
                bool  canLoop   = TrackHasLoop(loaded);
                bool  on        = g_player.loopWanted && canLoop;
                bool  hot       = canLoop && Clay_PointerOver(Clay_GetElementId(CLAY_STRING("LoopButton")));

                CLAY(CLAY_ID("LoopButton"), {
                    .layout = {
                        .sizing = { .width = CLAY_SIZING_FIXED(42), .height = CLAY_SIZING_FIXED(42) },
                        .childAlignment = { .x = CLAY_ALIGN_X_CENTER, .y = CLAY_ALIGN_Y_CENTER }
                    },
                    .backgroundColor = on ? C_ACCENT_DIM : (hot ? C_HOVER : C_PANEL_2),
                    .cornerRadius = CLAY_CORNER_RADIUS(21)
                }) {
                    CLAY_TEXT(CLAY_STRING("LOOP"),
                              CLAY_TEXT_CONFIG(TextCfgNoWrap(10,
                                  !canLoop ? C_TEXT_FAINT : (on ? C_TEXT : C_TEXT_DIM), FONT_BODY)));
                }
            }

            CLAY(CLAY_ID("NowPlayingText"), {
                .layout = {
                    .layoutDirection = CLAY_TOP_TO_BOTTOM,
                    .sizing = { .width = CLAY_SIZING_GROW(0) },
                    .padding = { .left = 8 },
                    .childGap = 3
                }
            }) {
                CLAY_TEXT(track ? Str(track->title) : CLAY_STRING("Nothing loaded"),
                          CLAY_TEXT_CONFIG(TextCfgNoWrap(20, track ? C_TEXT : C_TEXT_FAINT, FONT_TITLE)));
                CLAY_TEXT(track ? CLAY_STRING("")
                                : CLAY_STRING("Double click a track in the playlist"),
                          CLAY_TEXT_CONFIG(TextCfgNoWrap(14, C_TEXT_DIM, FONT_BODY)));
            }

            RenderVolumeControl();

            CLAY(CLAY_ID("BigTime"), { .layout = { .childAlignment = { .y = CLAY_ALIGN_Y_CENTER },
                                                   .padding = { .left = 8 } } }) {
                CLAY_TEXT(Fmt("%.*s / %.*s", (int)posStr.length, posStr.chars,
                                             (int)durStr.length, durStr.chars),
                          CLAY_TEXT_CONFIG(TextCfgNoWrap(16, C_TEXT, FONT_BODY)));
            }
        }

        RenderWaveform();

        // elapsed / remaining / total, under the waveform
        CLAY(CLAY_ID("TimeRow"), {
            .layout = { .sizing = { .width = CLAY_SIZING_GROW(0) }, .childGap = 8 }
        }) {
            CLAY_TEXT(posStr, CLAY_TEXT_CONFIG(TextCfg(13, C_ACCENT, FONT_BODY)));
            CLAY(CLAY_ID("TimeSpacer"), {
                .layout = { .sizing = { .width = CLAY_SIZING_GROW(0) },
                            .childAlignment = { .x = CLAY_ALIGN_X_CENTER } }
            }) {
                if (g_player.uid) {
                    CLAY_TEXT(Fmt("-%.*s", (int)remainStr.length, remainStr.chars),
                              CLAY_TEXT_CONFIG(TextCfg(13, C_TEXT_FAINT, FONT_BODY)));
                }
            }
            CLAY_TEXT(durStr, CLAY_TEXT_CONFIG(TextCfg(13, C_TEXT_DIM, FONT_BODY)));
        }
    }
}

static void RenderMetaRow(Clay_String label, Clay_String value) {
    CLAY_AUTO_ID({
        .layout = { .sizing = { .width = CLAY_SIZING_GROW(0) }, .childGap = 12,
                    .padding = { .top = 7, .bottom = 7 } },
        .border = { .width = { .bottom = 1 }, .color = C_LINE }
    }) {
        CLAY_AUTO_ID({ .layout = { .sizing = { .width = CLAY_SIZING_FIXED(90) } } }) {
            CLAY_TEXT(label, CLAY_TEXT_CONFIG(TextCfg(16, C_TEXT_FAINT, FONT_BODY)));
        }
        CLAY_TEXT(value, CLAY_TEXT_CONFIG(TextCfg(18, C_TEXT, FONT_BODY)));
    }
}

// Exactly one item selected -> full detail view.
static void RenderSingleItemDetails(Item *item, int index, bool busy) {
    bool isNull = (item->kind == TRACK_KIND_NULL);

    CLAY(CLAY_ID("DetailsInner"), {
        .layout = {
            .layoutDirection = CLAY_TOP_TO_BOTTOM,
            .sizing = { .width = CLAY_SIZING_GROW(0), .height = CLAY_SIZING_GROW(0) },
            .padding = { .left = 28, .right = 28, .top = 28, .bottom = 20 },
            .childGap = 22
        }
    }) {
        CLAY(CLAY_ID("DetailsHeader"), {
            .layout = { .sizing = { .width = CLAY_SIZING_GROW(0) }, .childGap = 22 }
        }) {
            CLAY(CLAY_ID("DetailsTitleCol"), {
                .layout = {
                    .layoutDirection = CLAY_TOP_TO_BOTTOM,
                    .sizing = { .width = CLAY_SIZING_GROW(0) },
                    .childGap = 8
                }
            }) {
                CLAY_TEXT(isNull ? CLAY_STRING("Null track") : Str(item->title),
                          CLAY_TEXT_CONFIG(TextCfg(30, (isNull || busy) ? C_TEXT_DIM : C_TEXT, FONT_TITLE)));
                CLAY_TEXT(isNull
                              ? CLAY_STRING("Padding slot. It holds no audio, but the engine counts it, so it has to keep this position.")
                              : Fmt("Slot %d of %d", index + 1, g_itemCount),
                          CLAY_TEXT_CONFIG(TextCfg(14, C_TEXT_FAINT, FONT_BODY)));

                // Nothing here may run while a background job is walking
                // g_items, so the buttons go away rather than sitting there
                // looking clickable and doing nothing.
                if (busy)
                {
                    CLAY_TEXT(CLAY_STRING("Editing is unavailable while the archive is being written."),
                              CLAY_TEXT_CONFIG(TextCfg(13, C_TEXT_FAINT, FONT_BODY)));
                }
                else
                {
                    // Replacing keeps the slot exactly where it is - that is
                    // the point, since the index is what the engine plays.
                    CLAY(CLAY_ID("TrackActions"), {
                        .layout = { .childGap = 8, .childAlignment = { .y = CLAY_ALIGN_Y_CENTER } }
                    }) {
                        CLAY(CLAY_ID("ReplaceButton"), {
                            .layout = {
                                .sizing = { .height = CLAY_SIZING_FIXED(34) },
                                .padding = { .left = 14, .right = 16 },
                                .childGap = 8,
                                .childAlignment = { .x = CLAY_ALIGN_X_CENTER, .y = CLAY_ALIGN_Y_CENTER }
                            },
                            .backgroundColor = Clay_Hovered() ? C_HOVER : C_PANEL_2,
                            .cornerRadius = CLAY_CORNER_RADIUS(6),
                            .border = { .width = CLAY_BORDER_OUTSIDE(1),
                                        .color = Clay_Hovered() ? C_ACCENT_DIM : C_LINE }
                        }) {
                            bool hot = Clay_Hovered();
                            IconFolder(hot ? C_ACCENT : C_TEXT_DIM);
                            CLAY_TEXT(isNull ? CLAY_STRING("Set audio from MP3/WAV")
                                             : CLAY_STRING("Replace with MP3/WAV"),
                                      CLAY_TEXT_CONFIG(TextCfgNoWrap(14, hot ? C_TEXT : C_TEXT_DIM, FONT_BODY)));
                        }

                        // Nothing to write out for a padding slot.
                        if (!isNull && ItemHasAudio(item)) {
                            CLAY(CLAY_ID("ExportButton"), {
                                .layout = {
                                    .sizing = { .height = CLAY_SIZING_FIXED(34) },
                                    .padding = { .left = 14, .right = 16 },
                                    .childGap = 8,
                                    .childAlignment = { .x = CLAY_ALIGN_X_CENTER, .y = CLAY_ALIGN_Y_CENTER }
                                },
                                .backgroundColor = Clay_Hovered() ? C_HOVER : C_PANEL_2,
                                .cornerRadius = CLAY_CORNER_RADIUS(6),
                                .border = { .width = CLAY_BORDER_OUTSIDE(1),
                                            .color = Clay_Hovered() ? C_ACCENT_DIM : C_LINE }
                            }) {
                                bool hot = Clay_Hovered();
                                IconDisk(hot ? C_ACCENT : C_TEXT_DIM, hot ? C_HOVER : C_PANEL_2);
                                CLAY_TEXT(CLAY_STRING("Export to WAV"),
                                          CLAY_TEXT_CONFIG(TextCfgNoWrap(14, hot ? C_TEXT : C_TEXT_DIM, FONT_BODY)));
                            }
                        }
                    }
                }
            }
        }

        CLAY(CLAY_ID("DetailsMeta"), {
            .layout = { .layoutDirection = CLAY_TOP_TO_BOTTOM,
                        .sizing = { .width = CLAY_SIZING_GROW(0) } }
        }) {
            RenderMetaRow(CLAY_STRING("Duration"), TimeStr(item->duration));
            RenderMetaRow(CLAY_STRING("Format"),   Str(item->format));
            if (ItemHasAudio(item)) {
                RenderMetaRow(CLAY_STRING("Audio"),
                              Fmt("%u Hz  -  %s  -  %d Bit",
                                  item->sampleRate,
                                  item->channels == 2 ? "Stereo" : "Mono",
                                  TRACK_BITS_PER_SAMPLE));
                // The source bytes, which is what the row actually holds. The
                // decoded size is what it expands to when something plays it.
                RenderMetaRow(CLAY_STRING("Size"),
                              item->adpcm
                                  ? Fmt("%.2f MB  (%.1f MB decoded)",
                                        (double)item->adpcmSize / (1024.0 * 1024.0),
                                        (double)item->pcmSize / (1024.0 * 1024.0))
                                  : Fmt("%.1f MB", (double)item->pcmSize / (1024.0 * 1024.0)));
            }
            // Imported tracks keep the path they came from purely as a label;
            // their samples live in memory like every other slot.
            RenderMetaRow(CLAY_STRING("Source"),   item->path[0] ? Str(item->path)
                                                                 : CLAY_STRING("In memory"));
        }
    }
}

static void RenderDetailsPanel(void) {
    int  selCount = SelectedCount();
    bool busy     = IsBusy();

    CLAY(CLAY_ID("DetailsPanel"), {
        .layout = {
            .layoutDirection = CLAY_TOP_TO_BOTTOM,
            .sizing = { .width = CLAY_SIZING_GROW(0), .height = CLAY_SIZING_GROW(0) }
        },
        .backgroundColor = C_BG,
        .clip = { .vertical = true, .childOffset = Clay_GetScrollOffset() }
    }) {
        if (selCount == 1) {
            int idx = FirstSelected();
            RenderSingleItemDetails(&g_items[idx], idx, busy);
        } else {
            CLAY(CLAY_ID("DetailsEmpty"), {
                .layout = {
                    .layoutDirection = CLAY_TOP_TO_BOTTOM,
                    .sizing = { .width = CLAY_SIZING_GROW(0), .height = CLAY_SIZING_GROW(0) },
                    .padding = { .left = 28, .right = 28, .top = 28, .bottom = 28 },
                    .childGap = 14,
                    .childAlignment = { .x = CLAY_ALIGN_X_CENTER, .y = CLAY_ALIGN_Y_CENTER }
                }
            }) {
                if (selCount == 0) {
                    CLAY_TEXT(CLAY_STRING("No track selected"),
                              CLAY_TEXT_CONFIG(TextCfg(22, C_TEXT_DIM, FONT_TITLE)));
                    CLAY_TEXT(CLAY_STRING("Select a single track on the right to see its details."),
                              CLAY_TEXT_CONFIG(TextCfg(14, C_TEXT_FAINT, FONT_BODY)));
                } else {
                    float total = 0.0f;
                    for (int i = 0; i < g_itemCount; i++) if (g_items[i].selected) total += g_items[i].duration;
                    Clay_String totalStr = TimeStr(total);
                    CLAY_TEXT(Fmt("%d tracks selected", selCount),
                              CLAY_TEXT_CONFIG(TextCfg(26, C_TEXT, FONT_TITLE)));
                    CLAY_TEXT(Fmt("Total running time %.*s", (int)totalStr.length, totalStr.chars),
                              CLAY_TEXT_CONFIG(TextCfg(15, C_TEXT_DIM, FONT_BODY)));
                    CLAY_TEXT(CLAY_STRING("Drag them anywhere in the list to reorder, or press delete to remove."),
                              CLAY_TEXT_CONFIG(TextCfg(13, C_TEXT_FAINT, FONT_BODY)));
                }
            }
        }
    }
}

// Width available to the title / artist column. Derived from the row box and
// fixed metrics only - never from the measured text - so it cannot feed back
// into the text that is measured against it.
static float RowTextBudget(int index, float durationWidth) {
    Clay_ElementData row = Clay_GetElementData(CLAY_IDI("ListItem", index));
    float rowW = row.found ? row.boundingBox.width : (LIST_WIDTH - 21.0f);
    return rowW - ROW_PAD_X * 2.0f - ROW_NUM_W - ROW_GAP * 2.0f - durationWidth;
}

static void RenderListItem(int i, float durationWidth) {
    Item *item = &g_items[i];
    bool selected   = item->selected;
    bool hovered    = (g_ui.hoverIndex == i);
    bool isPlaying  = (g_player.uid == item->uid);
    bool beingMoved = g_ui.dragging && selected;
    bool isNull     = (item->kind == TRACK_KIND_NULL);

    Clay_Color bg = selected ? (hovered ? C_SELECT_HOV : C_SELECT)
                             : (hovered ? C_HOVER : ((i & 1) ? C_ROW_ALT : C_ROW));
    if (isNull && !selected && !hovered) bg = C_BG;   // recessed, but still a row
    if (beingMoved) bg.a = 90;                        // ghosted while it travels

    // Borders are drawn inset and never affect layout, which makes them a
    // flicker free way to show where the drop will land.
    Clay_BorderWidth borderW = { 0 };
    if (isPlaying) borderW.left = 3;
    if (g_ui.dragging) {
        if (g_ui.dropIndex == i) borderW.top = 3;
        if (g_ui.dropIndex == g_itemCount && i == g_itemCount - 1) borderW.bottom = 3;
    }
    Clay_Color borderColor = (borderW.top || borderW.bottom) ? C_ACCENT : C_ACCENT_DIM;

    float budget = RowTextBudget(i, durationWidth);

    CLAY(CLAY_IDI("ListItem", i), {
        .layout = {
            .sizing = { .width = CLAY_SIZING_GROW(0), .height = CLAY_SIZING_FIXED(ROW_HEIGHT) },
            .padding = { .left = (uint16_t)ROW_PAD_X, .right = (uint16_t)ROW_PAD_X },
            .childGap = (uint16_t)ROW_GAP,
            .childAlignment = { .y = CLAY_ALIGN_Y_CENTER }
        },
        .backgroundColor = bg,
        .cornerRadius = CLAY_CORNER_RADIUS(6),
        .border = { .width = borderW, .color = borderColor }
    }) {
        // track number, or a play / pause marker for the loaded track
        CLAY(CLAY_IDI("ListItemNum", i), {
            .layout = { .sizing = { .width = CLAY_SIZING_FIXED(ROW_NUM_W) },
                        .childAlignment = { .x = CLAY_ALIGN_X_RIGHT } }
        }) {
            // The index is what the engine addresses, so it stays visible on
            // padding slots too.
            CLAY_TEXT(isPlaying ? (g_player.playing ? CLAY_STRING(">") : CLAY_STRING("=")) : Fmt("%d", i + 1),
                      CLAY_TEXT_CONFIG(TextCfgNoWrap(13, isPlaying ? C_ACCENT : C_TEXT_FAINT, FONT_BODY)));
        }

        // Note: no .clip here. A nested clip element becomes a scroll
        // container, and Clay routes the wheel to the innermost one under the
        // pointer - which would swallow wheel scrolling over the rows. The
        // titles are ellipsised instead.
        CLAY(CLAY_IDI("ListItemText", i), {
            .layout = { .layoutDirection = CLAY_TOP_TO_BOTTOM,
                        .sizing = { .width = CLAY_SIZING_GROW(0) },
                        .childGap = 3 }
        }) {
            if (isNull) {
                CLAY_TEXT(CLAY_STRING("Null track"),
                          CLAY_TEXT_CONFIG(TextCfgNoWrap(15, C_TEXT_FAINT, FONT_BODY)));
                CLAY_TEXT(CLAY_STRING("padding - keeps this index"),
                          CLAY_TEXT_CONFIG(TextCfgNoWrap(12, C_TEXT_FAINT, FONT_BODY)));
            } else {
                CLAY_TEXT(FitText(item->title, 15.0f, budget),
                          CLAY_TEXT_CONFIG(TextCfgNoWrap(15, isPlaying ? C_ACCENT : C_TEXT, FONT_BODY)));
                CLAY_TEXT(FitText(item->artist, 12.0f, budget),
                          CLAY_TEXT_CONFIG(TextCfgNoWrap(12, C_TEXT_DIM, FONT_BODY)));
            }
        }

        if (isNull) {
            CLAY(CLAY_IDI("ListItemBadge", i), {
                .layout = { .padding = { .left = 6, .right = 6, .top = 2, .bottom = 2 } },
                .cornerRadius = CLAY_CORNER_RADIUS(3),
                .border = { .width = CLAY_BORDER_OUTSIDE(1), .color = C_TEXT_FAINT }
            }) {
                CLAY_TEXT(CLAY_STRING("NULL"),
                          CLAY_TEXT_CONFIG(TextCfgNoWrap(11, C_TEXT_FAINT, FONT_BODY)));
            }
        } else {
            CLAY_TEXT(TimeStr(item->duration),
                      CLAY_TEXT_CONFIG(TextCfgNoWrap(13, C_TEXT_FAINT, FONT_BODY)));
        }
    }
}

static void RenderScrollbar(void) {
    Clay_ScrollContainerData sd = Clay_GetScrollContainerData(ListScrollId());
    if (!sd.found) return;

    float trackH   = sd.scrollContainerDimensions.height;
    float contentH = sd.contentDimensions.height;
    if (contentH <= trackH || trackH <= 0) return;

    float thumbH    = fmaxf((trackH / contentH) * trackH, 30.0f);
    float maxOffset = trackH - thumbH;
    float maxScroll = contentH - trackH;
    float t = maxScroll > 0 ? Clampf(-sd.scrollPosition->y / maxScroll, 0.0f, 1.0f) : 0.0f;

    bool thumbHot = g_ui.scrollbarDrag || Clay_PointerOver(Clay_GetElementId(CLAY_STRING("ScrollThumb")));

    CLAY(CLAY_ID("ScrollTrack"), {
        .layout = { .sizing = { .width = CLAY_SIZING_FIXED(SCROLLBAR_W),
                                .height = CLAY_SIZING_FIXED(trackH) } },
        .floating = {
            .offset = { .x = -3, .y = 0 },
            .zIndex = 6,
            .parentId = ListScrollId().id,
            .attachPoints = { .element = CLAY_ATTACH_POINT_RIGHT_TOP,
                              .parent  = CLAY_ATTACH_POINT_RIGHT_TOP },
            .attachTo = CLAY_ATTACH_TO_ELEMENT_WITH_ID
        },
        .backgroundColor = (Clay_Color){0, 0, 0, 70},
        .cornerRadius = CLAY_CORNER_RADIUS(SCROLLBAR_W / 2.0f)
    }) {}

    CLAY(CLAY_ID("ScrollThumb"), {
        .layout = { .sizing = { .width = CLAY_SIZING_FIXED(SCROLLBAR_W),
                                .height = CLAY_SIZING_FIXED(thumbH) } },
        .floating = {
            .offset = { .x = -3, .y = t * maxOffset },
            .zIndex = 7,
            .parentId = ListScrollId().id,
            .attachPoints = { .element = CLAY_ATTACH_POINT_RIGHT_TOP,
                              .parent  = CLAY_ATTACH_POINT_RIGHT_TOP },
            .attachTo = CLAY_ATTACH_TO_ELEMENT_WITH_ID
        },
        .backgroundColor = thumbHot ? C_ACCENT : (Clay_Color){86, 94, 112, 255},
        .cornerRadius = CLAY_CORNER_RADIUS(SCROLLBAR_W / 2.0f)
    }) {}
}

#define PROGRESS_W 240.0f

// Shown in place of the list while a load runs. The list is empty at that
// point anyway, so this covers the panel without needing a floating overlay
// measured against it.
static void RenderLoadingPanel(void) {
    TsMutexLock(&g_loader.mutex);
    int  done   = g_loader.done;
    int  total  = g_loader.total;
    char status[sizeof(g_loader.status)];
    snprintf(status, sizeof(status), "%s", g_loader.status);
    TsMutexUnlock(&g_loader.mutex);

    bool  determinate = (total > 0);
    float t = determinate ? Clampf((float)done / (float)total, 0.0f, 1.0f) : 0.0f;

    // Without a total yet, slide a short bar back and forth instead of
    // pretending to know how far along we are.
    float segW   = determinate ? t * PROGRESS_W : PROGRESS_W * 0.3f;
    float offset = 0.0f;
    if (!determinate) {
        float phase = (float)fmod(GetTime(), 1.6) / 1.6f;             // 0..1
        float tri   = phase < 0.5f ? phase * 2.0f : (1.0f - phase) * 2.0f;
        offset = tri * (PROGRESS_W - segW);
    }

    JobKind job = CurrentJob();

    CLAY(CLAY_ID("LoadingPanel"), {
        .layout = {
            .layoutDirection = CLAY_TOP_TO_BOTTOM,
            .sizing = { .width = CLAY_SIZING_GROW(0), .height = CLAY_SIZING_GROW(0) },
            .padding = { .left = 20, .right = 20 },
            .childGap = 14,
            .childAlignment = { .x = CLAY_ALIGN_X_CENTER, .y = CLAY_ALIGN_Y_CENTER }
        }
    }) {
        CLAY_TEXT(job == JOB_SAVE ? CLAY_STRING("Saving archive")
                                  : CLAY_STRING("Loading archive"),
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
                .layout = { .sizing = { .width = CLAY_SIZING_FIXED(segW),
                                        .height = CLAY_SIZING_FIXED(6) } },
                .backgroundColor = C_ACCENT,
                .cornerRadius = CLAY_CORNER_RADIUS(3)
            }) {}
        }

        // Both branches go through Fmt, which copies into the frame arena.
        // Str() would only alias `status`, a local that is gone by the time
        // Clay renders the string.
        CLAY_TEXT(determinate ? Fmt("%s  -  %d / %d", status, done, total) : Fmt("%s", status),
                  CLAY_TEXT_CONFIG(TextCfgNoWrap(13, C_TEXT_DIM, FONT_BODY)));
    }
}

static void RenderListPanel(void) {
    int  selCount = SelectedCount();
    bool busy     = IsBusy();

    Font font = g_fonts ? g_fonts[FONT_BODY] : GetFontDefault();
    if (!font.glyphs) font = GetFontDefault();
    float durationWidth = MeasureTextEx(font, "00:00", 13.0f, 0).x;

    CLAY(CLAY_ID("ListPanel"), {
        .layout = {
            .layoutDirection = CLAY_TOP_TO_BOTTOM,
            .sizing = { .width = CLAY_SIZING_FIXED(LIST_WIDTH), .height = CLAY_SIZING_GROW(0) }
        },
        .backgroundColor = C_PANEL,
        .border = { .width = { .left = 1 }, .color = C_LINE }
    }) {
        CLAY(CLAY_ID("ListHeader"), {
            .layout = {
                .layoutDirection = CLAY_TOP_TO_BOTTOM,
                .sizing = { .width = CLAY_SIZING_GROW(0) },
                .padding = { .left = 16, .right = 16, .top = 16, .bottom = 12 },
                .childGap = 3
            }
        }) {
            CLAY_TEXT(CLAY_STRING("Playlist"), CLAY_TEXT_CONFIG(TextCfg(18, C_TEXT, FONT_TITLE)));
            int nulls = NullCount();
            CLAY_TEXT(busy ? CLAY_STRING("working...")
                              : (selCount > 0
                                  ? Fmt("%d slots  -  %d null  -  %d selected", g_itemCount, nulls, selCount)
                                  : Fmt("%d slots  -  %d null", g_itemCount, nulls)),
                      CLAY_TEXT_CONFIG(TextCfgNoWrap(12, C_TEXT_FAINT, FONT_BODY)));
        }

        // Note: no early return here. CLAY() is a for-loop macro, so returning
        // out of an open element would skip Clay__CloseElement().
        if (busy) {
            RenderLoadingPanel();
        } else {
            // the scrolling list itself
            CLAY(CLAY_ID("ListScroll"), {
                .layout = {
                    .layoutDirection = CLAY_TOP_TO_BOTTOM,
                    .sizing = { .width = CLAY_SIZING_GROW(0), .height = CLAY_SIZING_GROW(0) },
                    .padding = { .left = 10, .right = 10, .top = 4, .bottom = 10 },
                    .childGap = 3
                },
                .clip = { .vertical = true, .childOffset = Clay_GetScrollOffset() }
            }) {
                for (int i = 0; i < g_itemCount; i++) RenderListItem(i, durationWidth);

                // drop line for an empty list
                if (g_ui.dragging && g_itemCount == 0) {
                    CLAY(CLAY_ID("DropEnd"), {
                        .layout = { .sizing = { .width = CLAY_SIZING_GROW(0),
                                                .height = CLAY_SIZING_FIXED(3) } },
                        .backgroundColor = C_ACCENT
                    }) {}
                }
            }

            RenderScrollbar();
        }
    }
}

// ---------------------------------------------------------------------------
// Menu bar
//
// The icons are built out of plain layout elements rather than an icon font or
// a texture, so there are no assets to ship and they scale with the theme.
// `cutout` should match whatever is behind the icon so the notches read as
// holes. Local IDs keep these reusable across buttons.
// ---------------------------------------------------------------------------

// Lower case "i" in a ring, drawn the same way as the other icons.
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

typedef enum {
    MENU_ICON_FOLDER, MENU_ICON_DISK, MENU_ICON_PLUS, MENU_ICON_NULL, MENU_ICON_TRASH, MENU_ICON_INFO
} MenuIcon;

static void RenderMenuButton(Clay_ElementId id, Clay_String label, MenuIcon icon, bool enabled) {
    // Not Clay_Hovered(): that reports the currently open element, which out
    // here is the menu bar, so every button would highlight together.
    bool hovered = enabled && Clay_PointerOver(id);
    bool held    = hovered && IsMouseButtonDown(MOUSE_BUTTON_LEFT);

    Clay_Color bg   = !enabled ? C_PANEL : (held ? C_SELECT : (hovered ? C_HOVER : C_PANEL_2));
    Clay_Color fg   = !enabled ? C_TEXT_FAINT : (hovered ? C_TEXT : C_TEXT_DIM);
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
        .border = { .width = CLAY_BORDER_OUTSIDE(1), .color = hovered ? C_ACCENT_DIM : C_LINE }
    }) {
        switch (icon) {
            case MENU_ICON_FOLDER: IconFolder(mark);     break;
            case MENU_ICON_DISK:   IconDisk(mark, bg);   break;
            case MENU_ICON_PLUS:   IconPlus(mark);       break;
            case MENU_ICON_NULL:   IconNull(mark);       break;
            case MENU_ICON_TRASH:  IconTrash(mark);      break;
            case MENU_ICON_INFO:   IconInfo(mark);       break;
        }
        CLAY_TEXT(label, CLAY_TEXT_CONFIG(TextCfgNoWrap(14, fg, FONT_BODY)));
    }
}

static void RenderMenuSeparator(void) {
    CLAY_AUTO_ID({
        .layout = { .sizing = { .width = CLAY_SIZING_FIXED(1), .height = CLAY_SIZING_FIXED(22) } },
        .backgroundColor = C_LINE
    }) {}
}

static void RenderMenuBar(void) {
    int  selCount = SelectedCount();
    bool busy     = IsBusy();

    CLAY(CLAY_ID("MenuBar"), {
        .layout = {
            .sizing = { .width = CLAY_SIZING_GROW(0), .height = CLAY_SIZING_FIXED(MENU_H) },
            .padding = { .left = 12, .right = 12 },
            .childGap = 8,
            .childAlignment = { .y = CLAY_ALIGN_Y_CENTER }
        },
        .backgroundColor = C_PANEL,
        .border = { .width = { .bottom = 1 }, .color = C_LINE }
    }) {
        RenderMenuButton(CLAY_ID("OpenButton"), CLAY_STRING("Open MH/MB"), MENU_ICON_FOLDER, !busy);
        RenderMenuButton(CLAY_ID("SaveButton"), CLAY_STRING("Save MH/MB"), MENU_ICON_DISK, !busy && g_itemCount > 0);

        RenderMenuSeparator();

        RenderMenuButton(CLAY_ID("AddTrackButton"), CLAY_STRING("Add track"), MENU_ICON_PLUS, !busy);
        RenderMenuButton(CLAY_ID("AddNullButton"),  CLAY_STRING("Add null"),  MENU_ICON_NULL, !busy);
        RenderMenuButton(CLAY_ID("RemoveButton"),
                         selCount > 1 ? Fmt("Remove %d", selCount) : CLAY_STRING("Remove"),
                         MENU_ICON_TRASH, !busy && selCount > 0);

        // Eats the leftover width so everything after it sits on the right.
        CLAY(CLAY_ID("MenuSpacer"), {
            .layout = { .sizing = { .width = CLAY_SIZING_GROW(0),
                                    .height = CLAY_SIZING_FIXED(1) } }
        }) {}

        // Standing reminder that closing or opening something else would
        // cost work, so the confirmation dialog is never a surprise.
        if (g_dirty) {
            CLAY(CLAY_ID("DirtyBadge"), {
                .layout = { .padding = { .left = 4, .right = 10 },
                            .childGap = 7,
                            .childAlignment = { .y = CLAY_ALIGN_Y_CENTER } }
            }) {
                CLAY(CLAY_ID("DirtyDot"), {
                    .layout = { .sizing = { .width = CLAY_SIZING_FIXED(7),
                                            .height = CLAY_SIZING_FIXED(7) } },
                    .backgroundColor = (Clay_Color){235, 170, 70, 255},
                    .cornerRadius = CLAY_CORNER_RADIUS(4)
                }) {}
                CLAY_TEXT(CLAY_STRING("Unsaved changes"),
                          CLAY_TEXT_CONFIG(TextCfgNoWrap(14, C_TEXT_DIM, FONT_BODY)));
            }
        }

        RenderMenuButton(CLAY_ID("AboutButton"), CLAY_STRING("About"), MENU_ICON_INFO, !busy);
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

    // Full window scrim. It captures the pointer, so nothing behind it reacts
    // to hover while the dialog is up.
    CLAY(CLAY_ID("AboutScrim"), {
        .layout = {
            .sizing = { .width = CLAY_SIZING_FIXED((float)GetScreenWidth()),
                        .height = CLAY_SIZING_FIXED((float)GetScreenHeight()) },
            .childAlignment = { .x = CLAY_ALIGN_X_CENTER, .y = CLAY_ALIGN_Y_CENTER }
        },
        .floating = {
            .zIndex = 100,
            .attachPoints = { .element = CLAY_ATTACH_POINT_LEFT_TOP,
                              .parent  = CLAY_ATTACH_POINT_LEFT_TOP },
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
            CLAY_TEXT(CLAY_STRING(APP_NAME),
                      CLAY_TEXT_CONFIG(TextCfg(24, C_TEXT, FONT_TITLE)));
            CLAY_TEXT(CLAY_STRING("Version " APP_VERSION),
                      CLAY_TEXT_CONFIG(TextCfgNoWrap(16, C_TEXT_FAINT, FONT_BODY)));

            CLAY_AUTO_ID({ .layout = { .sizing = { .width = CLAY_SIZING_GROW(0),
                                                   .height = CLAY_SIZING_FIXED(14) } } }) {}

            CLAY_TEXT(CLAY_STRING("Crash Twinsanity music and VA archives manager. Supports creating brand new archives, editing existing ones, track reordering, track replacement and track deletion."),
                      CLAY_TEXT_CONFIG(TextCfg(16, C_TEXT_DIM, FONT_BODY)));

            CLAY_AUTO_ID({ .layout = { .sizing = { .width = CLAY_SIZING_GROW(0),
                                                   .height = CLAY_SIZING_FIXED(16) } },
                           .border = { .width = { .bottom = 1 }, .color = C_LINE } }) {}

            CLAY_AUTO_ID({ .layout = { .sizing = { .width = CLAY_SIZING_GROW(0) },
                                       .padding = { .top = 10, .bottom = 4 } } }) {
                CLAY_TEXT(CLAY_STRING("Shortcuts"),
                          CLAY_TEXT_CONFIG(TextCfgNoWrap(16, C_TEXT, FONT_TITLE)));
            }

            RenderAboutRow(CLAY_STRING("Space"),             CLAY_STRING("Play / Pause"));
            RenderAboutRow(CLAY_STRING("Enter"),             CLAY_STRING("Load the selected track"));
            RenderAboutRow(CLAY_STRING("Up / Down"),         CLAY_STRING("Move the selection, hold Shift to extend"));
            RenderAboutRow(CLAY_STRING("Ctrl + Left Click"), CLAY_STRING("Add or remove one row from the selection"));
            RenderAboutRow(CLAY_STRING("Ctrl + A"),          CLAY_STRING("Select every slot"));
            RenderAboutRow(CLAY_STRING("Delete"),            CLAY_STRING("Remove the selected slots"));
            RenderAboutRow(CLAY_STRING("Drag"),              CLAY_STRING("Reorder; the blue line is where it lands"));
            RenderAboutRow(CLAY_STRING("Esc"),               CLAY_STRING("Close this dialog"));

            CLAY_AUTO_ID({ .layout = { .sizing = { .width = CLAY_SIZING_GROW(0),
                                                   .height = CLAY_SIZING_FIXED(16) } },
                           .border = { .width = { .bottom = 1 }, .color = C_LINE } }) {}

            CLAY_AUTO_ID({ .layout = { .sizing = { .width = CLAY_SIZING_GROW(0) },
                                       .padding = { .top = 12 },
                                       .childAlignment = { .y = CLAY_ALIGN_Y_CENTER } } }) {
                CLAY_TEXT(CLAY_STRING("Built with Clay, raylib and tinyfiledialogs"),
                          CLAY_TEXT_CONFIG(TextCfgNoWrap(14, C_TEXT_FAINT, FONT_BODY)));

                CLAY_AUTO_ID({ .layout = { .sizing = { .width = CLAY_SIZING_GROW(0),
                                                       .height = CLAY_SIZING_FIXED(1) } } }) {}

                CLAY(CLAY_ID("AboutCloseButton"), {
                    .layout = {
                        .sizing = { .width = CLAY_SIZING_FIXED(96), .height = CLAY_SIZING_FIXED(32) },
                        .childAlignment = { .x = CLAY_ALIGN_X_CENTER, .y = CLAY_ALIGN_Y_CENTER }
                    },
                    .backgroundColor = Clay_Hovered() ? (Clay_Color){120, 190, 255, 255} : C_ACCENT,
                    .cornerRadius = CLAY_CORNER_RADIUS(6)
                }) {
                    CLAY_TEXT(CLAY_STRING("Close"),
                              CLAY_TEXT_CONFIG(TextCfgNoWrap(16, (Clay_Color){12, 18, 28, 255}, FONT_BODY)));
                }
            }
        }
    }
}

// ---------------------------------------------------------------------------
// Unsaved-changes dialog
// ---------------------------------------------------------------------------

// Three answers, so three buttons rather than the usual yes/no: discarding
// work and saving it are both one click away, and neither is the default.
typedef enum { CONFIRM_BTN_PRIMARY, CONFIRM_BTN_DANGER, CONFIRM_BTN_QUIET } ConfirmButtonStyle;

static void RenderConfirmButton(Clay_ElementId id, Clay_String label, ConfirmButtonStyle style) {
    // Not Clay_Hovered(): out here that reports the row, so all three would
    // light up together.
    bool hovered = Clay_PointerOver(id);

    Clay_Color bg, fg;
    switch (style) {
        case CONFIRM_BTN_PRIMARY:
            bg = hovered ? (Clay_Color){120, 190, 255, 255} : C_ACCENT;
            fg = (Clay_Color){12, 18, 28, 255};
            break;
        case CONFIRM_BTN_DANGER:
            bg = hovered ? (Clay_Color){168, 62, 62, 255} : (Clay_Color){122, 46, 46, 255};
            fg = (Clay_Color){250, 228, 228, 255};
            break;
        default:
            bg = hovered ? C_HOVER : C_PANEL_2;
            fg = hovered ? C_TEXT : C_TEXT_DIM;
            break;
    }

    CLAY(id, {
        .layout = {
            .sizing = { .width = CLAY_SIZING_FIXED(110), .height = CLAY_SIZING_FIXED(34) },
            .childAlignment = { .x = CLAY_ALIGN_X_CENTER, .y = CLAY_ALIGN_Y_CENTER }
        },
        .backgroundColor = bg,
        .cornerRadius = CLAY_CORNER_RADIUS(6),
        .border = { .width = CLAY_BORDER_OUTSIDE(1),
                    .color = style == CONFIRM_BTN_QUIET ? C_LINE : bg }
    }) {
        CLAY_TEXT(label, CLAY_TEXT_CONFIG(TextCfgNoWrap(16, fg, FONT_BODY)));
    }
}

static void RenderConfirmDialog(void) {
    if (!g_ui.confirmOpen) return;

    bool exiting = (g_ui.pendingAction == PENDING_EXIT);
    int  edited  = g_itemCount;

    // zIndex above the About scrim: if both were somehow up, this is the one
    // that has to be answered.
    CLAY(CLAY_ID("ConfirmScrim"), {
        .layout = {
            .sizing = { .width = CLAY_SIZING_FIXED((float)GetScreenWidth()),
                        .height = CLAY_SIZING_FIXED((float)GetScreenHeight()) },
            .childAlignment = { .x = CLAY_ALIGN_X_CENTER, .y = CLAY_ALIGN_Y_CENTER }
        },
        .floating = {
            .zIndex = 110,
            .attachPoints = { .element = CLAY_ATTACH_POINT_LEFT_TOP,
                              .parent  = CLAY_ATTACH_POINT_LEFT_TOP },
            .pointerCaptureMode = CLAY_POINTER_CAPTURE_MODE_CAPTURE,
            .attachTo = CLAY_ATTACH_TO_ROOT
        },
        .backgroundColor = (Clay_Color){0, 0, 0, 180}
    }) {
        CLAY(CLAY_ID("ConfirmDialog"), {
            .layout = {
                .layoutDirection = CLAY_TOP_TO_BOTTOM,
                .sizing = { .width = CLAY_SIZING_FIXED(480) },
                .padding = { .left = 28, .right = 28, .top = 24, .bottom = 22 },
                .childGap = 6
            },
            .backgroundColor = C_PANEL,
            .cornerRadius = CLAY_CORNER_RADIUS(12),
            .border = { .width = CLAY_BORDER_OUTSIDE(1), .color = C_LINE }
        }) {
            CLAY_TEXT(exiting ? CLAY_STRING("Quit without saving?")
                              : CLAY_STRING("Open another archive?"),
                      CLAY_TEXT_CONFIG(TextCfg(20, C_TEXT, FONT_TITLE)));

            CLAY_AUTO_ID({ .layout = { .sizing = { .width = CLAY_SIZING_GROW(0),
                                                   .height = CLAY_SIZING_FIXED(10) } } }) {}

            CLAY_TEXT(Fmt("The playlist has %d slot%s with changes that have not been written "
                          "to an MH/MB pair yet.",
                          edited, edited == 1 ? "" : "s"),
                      CLAY_TEXT_CONFIG(TextCfg(16, C_TEXT_DIM, FONT_BODY)));

            CLAY_AUTO_ID({ .layout = { .sizing = { .width = CLAY_SIZING_GROW(0),
                                                   .height = CLAY_SIZING_FIXED(6) } } }) {}

            CLAY_TEXT(exiting ? CLAY_STRING("Closing the window loses them.")
                              : CLAY_STRING("Opening another archive replaces the whole playlist "
                                            "and loses them."),
                      CLAY_TEXT_CONFIG(TextCfg(16, C_TEXT_DIM, FONT_BODY)));

            CLAY_AUTO_ID({ .layout = { .sizing = { .width = CLAY_SIZING_GROW(0),
                                                   .height = CLAY_SIZING_FIXED(18) } },
                           .border = { .width = { .bottom = 1 }, .color = C_LINE } }) {}

            CLAY_AUTO_ID({ .layout = { .sizing = { .width = CLAY_SIZING_GROW(0) },
                                       .padding = { .top = 16 },
                                       .childGap = 8,
                                       .childAlignment = { .y = CLAY_ALIGN_Y_CENTER } } }) {
                RenderConfirmButton(CLAY_ID("ConfirmCancelButton"),
                                    CLAY_STRING("Cancel"), CONFIRM_BTN_QUIET);

                // Pushes the two committing answers away from Cancel, so the
                // destructive one is nowhere near the pointer's resting place.
                CLAY_AUTO_ID({ .layout = { .sizing = { .width = CLAY_SIZING_GROW(0),
                                                       .height = CLAY_SIZING_FIXED(1) } } }) {}

                RenderConfirmButton(CLAY_ID("ConfirmDiscardButton"),
                                    exiting ? CLAY_STRING("Quit anyway") : CLAY_STRING("Discard"),
                                    CONFIRM_BTN_DANGER);
                RenderConfirmButton(CLAY_ID("ConfirmSaveButton"),
                                    CLAY_STRING("Save first"), CONFIRM_BTN_PRIMARY);
            }
        }
    }
}


static void RenderDragGhost(Vector2 mouse) {
    if (!g_ui.dragging) return;
    int count = SelectedCount();
    int first = FirstSelected();
    if (first < 0) return;

    CLAY(CLAY_ID("DragGhost"), {
        .layout = {
            .padding = { .left = 14, .right = 14, .top = 10, .bottom = 10 },
            .childGap = 8,
            .childAlignment = { .y = CLAY_ALIGN_Y_CENTER }
        },
        .floating = {
            .offset = { .x = mouse.x + 16, .y = mouse.y + 12 },
            .zIndex = 64,
            .attachPoints = { .element = CLAY_ATTACH_POINT_LEFT_TOP,
                              .parent  = CLAY_ATTACH_POINT_LEFT_TOP },
            .pointerCaptureMode = CLAY_POINTER_CAPTURE_MODE_PASSTHROUGH,
            .attachTo = CLAY_ATTACH_TO_ROOT
        },
        .backgroundColor = (Clay_Color){28, 64, 110, 240},
        .cornerRadius = CLAY_CORNER_RADIUS(8),
        .border = { .width = CLAY_BORDER_OUTSIDE(1), .color = C_ACCENT }
    }) {
        CLAY_TEXT(count == 1 ? FitText(g_items[first].title, 14.0f, 240.0f) : Fmt("%d tracks", count),
                  CLAY_TEXT_CONFIG(TextCfgNoWrap(14, C_TEXT, FONT_BODY)));
        CLAY_TEXT(Fmt("-> %d", g_ui.dropIndex + 1),
                  CLAY_TEXT_CONFIG(TextCfgNoWrap(13, C_ACCENT, FONT_BODY)));
    }
}

static Clay_RenderCommandArray BuildLayout(Vector2 mouse, float dt) {
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
            .layout = {
                .layoutDirection = CLAY_LEFT_TO_RIGHT,
                .sizing = { .width = CLAY_SIZING_GROW(0), .height = CLAY_SIZING_GROW(0) }
            }
        }) {
            CLAY(CLAY_ID("LeftColumn"), {
                .layout = {
                    .layoutDirection = CLAY_TOP_TO_BOTTOM,
                    .sizing = { .width = CLAY_SIZING_GROW(0), .height = CLAY_SIZING_GROW(0) }
                }
            }) {
                RenderDetailsPanel();
                RenderPlayerBar();
            }

            RenderListPanel();
        }

        // Attached to the root, so it stays above the menu bar too.
        RenderDragGhost(mouse);
        RenderAboutDialog();
        RenderConfirmDialog();
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

// Clay's arena is ~11 MB at 16384 elements and doubles on a capacity error.
// The block is kept here so the re-init can release the old one: each growth
// used to strand the previous arena, and they are not small.
static void *g_clayMemory = NULL;

static void InitClay(void) {
    uint64_t size = Clay_MinMemorySize();
    void *block = malloc(size);
    if (!block) { fprintf(stderr, "Out of memory for Clay's arena\n"); return; }

    free(g_clayMemory);          // no-op on the first call
    g_clayMemory = block;

    Clay_Arena arena = Clay_CreateArenaWithCapacityAndMemory(size, block);
    Clay_Initialize(arena,
                    (Clay_Dimensions){ (float)GetScreenWidth(), (float)GetScreenHeight() },
                    (Clay_ErrorHandler){ HandleClayErrors, 0 });
}

int main(int argc, char **argv) {
    Clay_Raylib_Initialize(1280, 800, "Twinsanity Music/VA Manager",
                           FLAG_VSYNC_HINT | FLAG_WINDOW_RESIZABLE | FLAG_MSAA_4X_HINT);
    SetExitKey(KEY_NULL);
    InitAudioDevice();

    // Every stream created from here on uses this sub-buffer size, which is
    // what STREAM_LATENCY_FRAMES assumes when it backs the play cursor off.
    SetAudioStreamBufferSizeDefault((int)STREAM_CHUNK_FRAMES);

    TsMutexInit(&g_loader.mutex);
    g_loader.state = LOAD_IDLE;

    Clay_SetMaxElementCount(16384);
    InitClay();
    TwinStudio_UiInit();

    Font* fonts = TwinStudio_GetUiContext()->fonts;
    g_fonts = fonts;

    if (g_itemCount > 0) { SelectOnly(0); g_ui.anchorIndex = 0; }

    bool titleDirty = false;

    while (!g_quit) {
        // Consumed here rather than in the loop condition: a close request
        // with unsaved work has to raise the dialog instead of leaving.
        if (WindowShouldClose()) {
            if (IsBusy()) {
                // A worker is walking g_items; tearing down now would pull
                // the playlist out from under it. Ignore the request.
                fprintf(stderr, "Close ignored: an archive job is running\n");
            } else if (g_ui.confirmOpen) {
                // Already asking about something else - closing the window is
                // the stronger intent, so the same dialog now asks about that.
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

        // Pointer state first: every handler below queries last frame's boxes.
        Clay_SetLayoutDimensions((Clay_Dimensions){ (float)GetScreenWidth(), (float)GetScreenHeight() });
        Clay_SetPointerState((Clay_Vector2){ mouse.x, mouse.y },
                             IsMouseButtonDown(MOUSE_BUTTON_LEFT) && !g_ui.scrollbarDrag);

        // A row appended last frame now has a box, so it can be scrolled to.
        if (!IsBusy() && g_ui.pendingScrollIndex >= 0) {
            ScrollRowIntoView(g_ui.pendingScrollIndex);
            g_ui.pendingScrollIndex = -1;
        }

        // Publish a finished background load before anything reads g_items.
        PumpArchiveLoader();
        const bool busy = IsBusy();

        // Opening and saving both walk g_items on another thread, so no
        // handler that could reorder, delete or replace a row may run. Drags
        // in progress are dropped rather than left half-finished.
        if (g_ui.confirmOpen) {
            // Outranks everything, About included: it has to be answered.
            g_ui.dragging = g_ui.dragCandidate = g_ui.pressedInList = false;
            g_ui.scrollbarDrag = g_ui.scrubbing = g_ui.volumeDrag = false;
            g_ui.dropIndex = -1;
            g_ui.hoverIndex = -1;
            HandleConfirmDialog(mouse);
        } else if (g_ui.aboutOpen) {
            // Modal: only the dialog responds, and any drag in flight is dropped.
            g_ui.dragging = g_ui.dragCandidate = g_ui.pressedInList = false;
            g_ui.scrollbarDrag = g_ui.scrubbing = g_ui.volumeDrag = false;
            g_ui.dropIndex = -1;
            g_ui.hoverIndex = -1;
            HandleAboutDialog(mouse);
        } else if (busy) {
            g_ui.dragging = g_ui.dragCandidate = g_ui.pressedInList = false;
            g_ui.scrollbarDrag = g_ui.scrubbing = g_ui.volumeDrag = false;
            g_ui.dropIndex = -1;
            g_ui.hoverIndex = -1;
        } else {
            HandleScrollbar(mouse);
            if (!g_ui.scrubbing) HandleVolumeSlider(mouse);
            if (!g_ui.volumeDrag) HandleWaveform(mouse);
            if (!g_ui.scrubbing && !g_ui.volumeDrag) {
                HandleMenuBar();
                HandleTransportButtons();
                HandleListInput(mouse, dt);
            }
            HandleKeyboard();
        }
        UpdatePlayback(dt);

        // Answered "quit": leave before drawing another frame.
        if (g_quit) break;

        // The window title carries the flag too, for when the app is not the
        // window being looked at.
        if (g_dirty != titleDirty) {
            titleDirty = g_dirty;
            SetWindowTitle(g_dirty ? APP_NAME " *" : APP_NAME);
        }

        Vector2 wheel = GetMouseWheelMoveV();
        // Drag scrolling is off: the left button is already used for reordering.
        Clay_UpdateScrollContainers(false, (Clay_Vector2){ wheel.x * 12.0f, wheel.y * 12.0f }, dt);

        ResetFrameStrings();
        Clay_RenderCommandArray commands = BuildLayout(mouse, dt);

        BeginDrawing();
        ClearBackground(BLACK);
        Clay_Raylib_Render(commands, fonts);
        EndDrawing();
    }

    // The worker writes into g_loadStaging and allocates buffers, so it has to
    // be finished before any of that is torn down.
    if (g_loader.threadValid) {
        fprintf(stderr, "Waiting for the archive loader to finish...\n");
        TsThreadJoin(&g_loader.thread);
        g_loader.threadValid = false;
    }
    FreeStaging();
    TsMutexFree(&g_loader.mutex);

    UnloadPlaylistAudio();
    CloseAudioDevice();
    Clay_Raylib_Close();
    return 0;
}