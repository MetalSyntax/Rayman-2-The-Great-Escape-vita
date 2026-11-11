/**
 * @file coreaudio.c
 * @brief iOS AudioSession, AudioFile + AudioQueue (Rayman 2 BGM) and OpenAL (SFX) on sceAudioOut.
 * @details Handles guest audio output (such as embedded FMOD Ex or custom mixers):
 *          creates a RemoteIO unit, configures stream format and render callbacks,
 *          and streams audio on a dedicated high-priority sceAudioOut thread.
 *          Calls the guest callback via call_guest6.
 */
#include <AL/al.h>
#include <AL/alc.h>
#include <psp2/audioout.h>
#include <stdio.h>
#include <psp2/kernel/threadmgr.h>
#include <pthread.h>
#include <stdlib.h>
#include <string.h>
#include <opus/opusfile.h>

#include "darwin.h"
#include "imports.h"
#include "log.h"
#include "macho.h"
#include "objc_rt.h"

#define kAudioUnitProperty_StreamFormat 8
#define kAudioUnitProperty_SetRenderCallback 23
#define kAudioFormatFlagIsFloat (1 << 0)
#define kAudioFormatFlagIsNonInterleaved (1 << 5)
#define kAudioFormatFlagsFixed824 (24 << 7) // kLinearPCMFormatFlagsSampleFractionShift
#define FOURCC(a, b, c, d) (((a) << 24) | ((b) << 16) | ((c) << 8) | (d))

#define GRAIN 512

typedef struct {
    double mSampleRate;
    uint32_t mFormatID, mFormatFlags, mBytesPerPacket, mFramesPerPacket, mBytesPerFrame,
        mChannelsPerFrame, mBitsPerChannel, mReserved;
} asbd;

typedef struct {
    uint32_t mNumberBuffers;
    struct {
        uint32_t mNumberChannels, mDataByteSize;
        void *mData;
    } mBuffers[2];
} buffer_list;

typedef struct {
    asbd fmt;
    void *proc, *refcon;
    SceUID thread;
    volatile int running;
} audio_unit;

static int ok(void) { return 0; }

static int au_FindNext(void *prev, const void *desc) { return prev ? 0 : 1; } // single audio component

static int au_InstanceNew(void *comp, audio_unit **out) {
    audio_unit *u = calloc(1, sizeof(*u));
    // RemoteIO default: 44.1 kHz, s16 interleaved stereo.
    u->fmt = (asbd){44100, FOURCC('l', 'p', 'c', 'm'), 0xC, 4, 1, 4, 2, 16, 0};
    *out = u;
    return 0;
}

static int au_SetProperty(audio_unit *u, uint32_t id, uint32_t scope, uint32_t elem, const void *data, uint32_t size) {
    if (id == kAudioUnitProperty_StreamFormat) {
        memcpy(&u->fmt, data, sizeof(asbd));
        l_info("AudioUnit: %d Hz, %u channels, %u bits, flags 0x%X", (int)u->fmt.mSampleRate,
               (unsigned)u->fmt.mChannelsPerFrame, (unsigned)u->fmt.mBitsPerChannel, (unsigned)u->fmt.mFormatFlags);
    } else if (id == kAudioUnitProperty_SetRenderCallback) {
        u->proc = ((void *const *)data)[0];
        u->refcon = ((void *const *)data)[1];
    }
    return 0;
}

static int au_GetProperty(audio_unit *u, uint32_t id, uint32_t scope, uint32_t elem, void *data, uint32_t *size) {
    if (id == kAudioUnitProperty_StreamFormat) {
        memcpy(data, &u->fmt, sizeof(asbd));
        *size = sizeof(asbd);
        return 0;
    }
    memset(data, 0, *size);
    return 0;
}

static int16_t to_s16(const audio_unit *u, const uint8_t *p) {
    uint32_t fl = u->fmt.mFormatFlags;
    if (fl & kAudioFormatFlagIsFloat) {
        float f = *(const float *)p * 32767.0f;
        return f > 32767 ? 32767 : f < -32768 ? -32768 : (int16_t)f;
    }
    if (u->fmt.mBitsPerChannel == 32) { // 8.24 fixed-point (canonical AudioUnit) or s32
        int32_t v = *(const int32_t *)p >> ((fl & kAudioFormatFlagsFixed824) ? 9 : 16);
        return v > 32767 ? 32767 : v < -32768 ? -32768 : v;
    }
    return *(const int16_t *)p;
}

static int audio_thread(SceSize args, void *argp) {
    audio_unit *u = *(audio_unit **)argp;
    int rate = (int)u->fmt.mSampleRate, ch = u->fmt.mChannelsPerFrame;
    int bytes = u->fmt.mBitsPerChannel / 8;
    int nonint = (u->fmt.mFormatFlags & kAudioFormatFlagIsNonInterleaved) && ch == 2;
    int port = sceAudioOutOpenPort(SCE_AUDIO_OUT_PORT_TYPE_BGM, GRAIN, rate, SCE_AUDIO_OUT_MODE_STEREO);
    if (port < 0) {
        l_error("sceAudioOutOpenPort(%d Hz) = 0x%08X", rate, port);
        return 0;
    }
    uint8_t *raw = malloc(GRAIN * 2 * 4);
    int16_t out[GRAIN * 2];
    uint8_t ts[64] = {0}; // AudioTimeStamp
    uint32_t flags = 0;
    double sample_time = 0;
    buffer_list bl;
    while (u->running) {
        if (nonint)
            bl = (buffer_list){2, {{1, GRAIN * bytes, raw}, {1, GRAIN * bytes, raw + GRAIN * 4}}};
        else
            bl = (buffer_list){1, {{ch, GRAIN * ch * bytes, raw}}};
        memcpy(ts, &sample_time, 8);
        ((uint32_t *)ts)[14] = 1; // mFlags = kAudioTimeStampSampleTimeValid
        flags = 0;
        call_guest6(u->proc, (uint32_t)u->refcon, (uint32_t)&flags, (uint32_t)ts, 0, GRAIN, (uint32_t)&bl);
        sample_time += GRAIN;
        for (int i = 0; i < GRAIN; i++) {
            if (nonint) {
                out[i * 2] = to_s16(u, raw + i * bytes);
                out[i * 2 + 1] = to_s16(u, raw + GRAIN * 4 + i * bytes);
            } else {
                out[i * 2] = to_s16(u, raw + i * ch * bytes);
                out[i * 2 + 1] = ch == 2 ? to_s16(u, raw + (i * 2 + 1) * bytes) : out[i * 2];
            }
        }
        sceAudioOutOutput(port, out);
    }
    sceAudioOutReleasePort(port);
    free(raw);
    return 0;
}

static int au_Start(audio_unit *u) {
    if (u->running || !u->proc)
        return 0;
    u->running = 1;
    u->thread = sceKernelCreateThread("coreaudio", audio_thread, 0x10000100 - 20, 256 * 1024, 0, 0, NULL);
    sceKernelStartThread(u->thread, sizeof(u), &u);
    return 0;
}

static int au_Stop(audio_unit *u) {
    if (u->running) {
        u->running = 0;
        sceKernelWaitThreadEnd(u->thread, NULL, NULL);
        sceKernelDeleteThread(u->thread);
    }
    return 0;
}

static int au_Dispose(audio_unit *u) {
    au_Stop(u);
    free(u);
    return 0;
}

static int au_Render(void) { return -1; } // audio input (microphone): not supported

// AudioSession: queried by audio engines to select stream properties.
static int as_GetProperty(uint32_t id, uint32_t *size, void *data) {
    memset(data, 0, *size);
    switch (id) {
    case FOURCC('c', 'h', 's', 'r'): // CurrentHardwareSampleRate (Float64)
        *(double *)data = 44100.0;
        break;
    case FOURCC('c', 'h', 'o', 'c'): // CurrentHardwareOutputNumberChannels
        *(uint32_t *)data = 2;
        break;
    case FOURCC('c', 'h', 'b', 'd'): // CurrentHardwareIOBufferDuration (Float32)
        *(float *)data = (float)GRAIN / 44100.0f;
        break;
    case FOURCC('c', 'h', 'o', 'l'): // CurrentHardwareOutputLatency (Float32)
        *(float *)data = 0.012f;
        break;
    default:
        l_debug("AudioSessionGetProperty('%c%c%c%c') -> 0", id >> 24, (id >> 16) & 0xFF, (id >> 8) & 0xFF, id & 0xFF);
    }
    return 0;
}


// ---------------------------------------------------------------- AudioFile (BGM, Ogg Opus)
// Rayman 2's CBGMPlayer (Mach-O 0xc1520..0xc1f2c) streams DATA/WORLD/SOUNDIPHONE/*.BGM with
// AudioFileOpenURL + AudioFileReadPackets into an AudioQueue. The .BGM files are AAC (m4af), which
// has no decoder here: tools/stage_data.sh re-encodes them in place to Ogg Opus (always 48 kHz) and
// the AudioFile below decodes them with libopusfile as 16-bit stereo LPCM (1 frame = 1 packet), the
// rate the sceAudioOut MAIN port wants, so the queue never resamples.
// The game only queries 'dfmt', 'bcnt', 'pkub', 'pcnt' and optionally 'mgic'/'cmap' (absent here).

#define kAudioFileUnspecifiedError FOURCC('w', 'h', 't', '?')
#define kAudioFileUnsupportedPropertyError FOURCC('p', 't', 'y', '?')

typedef struct {
    OggOpusFile *of;
    int ch, rate;
    int64_t frames, pos; // total PCM frames, next frame op_read_stereo returns
} afile;

static int af_open(id url, int perm, uint32_t hint, afile **out) {
    char p[512];
    darwin_path(ns_cstr(url), p, sizeof(p));
    int r;
    OggOpusFile *of = op_open_file(p, &r);
    if (!of) {
        l_warn("[AUDIO] AudioFileOpenURL(%s): not Ogg Opus (%d), run tools/stage_data.sh", p, r);
        *out = NULL;
        return kAudioFileUnspecifiedError;
    }
    afile *f = calloc(1, sizeof(*f));
    f->of = of;
    f->ch = 2; // op_read_stereo
    f->rate = 48000;
    f->frames = op_pcm_total(of, -1);
    l_info("[AUDIO] BGM open %s: %d Hz, %d ch, %lld frames", p, f->rate, f->ch, (long long)f->frames);
    *out = f;
    return 0;
}

static int af_close(afile *f) {
    if (f) {
        op_free(f->of);
        free(f);
    }
    return 0;
}

static int af_get_info(afile *f, uint32_t id, uint32_t *size, uint32_t *writable) {
    uint32_t n = id == FOURCC('d', 'f', 'm', 't') ? sizeof(asbd)
               : id == FOURCC('b', 'c', 'n', 't') || id == FOURCC('p', 'c', 'n', 't') ? 8
               : id == FOURCC('p', 'k', 'u', 'b') ? 4 : 0;
    if (!f || !n)
        return kAudioFileUnsupportedPropertyError; // 'mgic' / 'cmap': LPCM has neither
    if (size)
        *size = n;
    if (writable)
        *writable = 0;
    return 0;
}

static int af_get(afile *f, uint32_t id, uint32_t *size, void *data) {
    if (!f)
        return kAudioFileUnspecifiedError;
    uint32_t bpf = f->ch * 2;
    switch (id) {
    case FOURCC('d', 'f', 'm', 't'): // AudioStreamBasicDescription
        *(asbd *)data = (asbd){f->rate, FOURCC('l', 'p', 'c', 'm'), 0xC /* signed | packed */, bpf, 1, bpf, f->ch, 16, 0};
        *size = sizeof(asbd);
        return 0;
    case FOURCC('b', 'c', 'n', 't'): // AudioDataByteCount (UInt64)
        *(uint64_t *)data = (uint64_t)f->frames * bpf;
        *size = 8;
        return 0;
    case FOURCC('p', 'c', 'n', 't'): // AudioDataPacketCount (UInt64)
        *(uint64_t *)data = (uint64_t)f->frames;
        *size = 8;
        return 0;
    case FOURCC('p', 'k', 'u', 'b'): // PacketSizeUpperBound (UInt32)
        *(uint32_t *)data = bpf;
        *size = 4;
        return 0;
    }
    l_debug("[AUDIO] AudioFileGetProperty('%c%c%c%c') unsupported", id >> 24, (id >> 16) & 0xFF, (id >> 8) & 0xFF, id & 0xFF);
    return kAudioFileUnsupportedPropertyError;
}

// AudioFileReadPackets(file, useCache, &ioNumBytes, packetDescs, SInt64 startPacket (stack, 8-aligned),
//                      &ioNumPackets, buffer): packets are PCM frames.
static int af_read_packets(afile *f, int use_cache, uint32_t *num_bytes, void *descs, int64_t start,
                           uint32_t *num_packets, void *buf) {
    if (!f)
        return kAudioFileUnspecifiedError;
    uint32_t bpf = f->ch * 2, want = *num_packets * bpf, got = 0;
    if (start != f->pos) {
        if (op_pcm_seek(f->of, start) < 0) {
            *num_packets = *num_bytes = 0;
            return 0;
        }
        f->pos = start;
    }
    while (got < want) {
        int r = op_read_stereo(f->of, (opus_int16 *)((char *)buf + got), (want - got) / 2);
        if (r <= 0)
            break; // end of stream (or a hole): the game loops/stops on 0 packets
        got += r * bpf;
    }
    f->pos += got / bpf;
    *num_packets = got / bpf;
    *num_bytes = got;
    return 0;
}

// ---------------------------------------------------------------- AudioQueue output (BGM)
// One sceAudioOut MAIN port (48 kHz stereo; OpenAL owns the single BGM port) per running queue.
// Enqueued buffers play in order; when one is fully played it goes back to the game's callback
// (call_guest), which refills it with AudioFileReadPackets and enqueues it again.
//
// Deadlock rule: CBGMPlayer::Clean() holds the game's g_queueMutex while calling AudioQueueFreeBuffer
// and AudioQueueDispose, and the callback takes that same mutex. So nothing here ever waits for the
// audio thread: Dispose only flags the queue and the thread frees it (and its buffers) on exit.

typedef struct {
    uint32_t mAudioDataBytesCapacity;
    void *mAudioData;
    uint32_t mAudioDataByteSize;
    void *mUserData;
    uint32_t mPacketDescriptionCapacity;
    void *mPacketDescriptions;
    uint32_t mPacketDescriptionCount;
} aq_buffer;

#define AQ_MAX 16
typedef struct {
    asbd fmt;
    void *cb, *user;
    aq_buffer *owned[AQ_MAX], *fifo[AQ_MAX];
    int nowned, nfifo;
    pthread_mutex_t lock;
    SceUID thread;
    volatile int running, dead;
    volatile float volume;
} aqueue;

#define AQ_GRAIN 1024
#define AQ_RATE 48000

static int aq_thread(SceSize args, void *argp) {
    aqueue *q = *(aqueue **)argp;
    int port = sceAudioOutOpenPort(SCE_AUDIO_OUT_PORT_TYPE_MAIN, AQ_GRAIN, AQ_RATE, SCE_AUDIO_OUT_MODE_STEREO);
    if (port < 0)
        l_error("[AUDIO] AudioQueue: sceAudioOutOpenPort(MAIN) = 0x%08X (BGM muted)", port);
    else
        l_info("[AUDIO] AudioQueue thread up: port %d, source %d Hz %u ch", port, (int)q->fmt.mSampleRate,
               (unsigned)q->fmt.mChannelsPerFrame);
    static int16_t out[AQ_GRAIN * 2];
    int n = 0, ch = q->fmt.mChannelsPerFrame == 1 ? 1 : 2;
    uint32_t step = (uint32_t)(q->fmt.mSampleRate * 65536.0 / AQ_RATE); // 16.16 source frames per output frame
    float vol_set = -1;
    while (!q->dead) {
        pthread_mutex_lock(&q->lock);
        aq_buffer *b = q->running && q->nfifo ? q->fifo[0] : NULL;
        pthread_mutex_unlock(&q->lock);
        if (!b) {
            sceKernelDelayThread(4000);
            continue;
        }
        const int16_t *src = b->mAudioData;
        uint32_t frames = b->mAudioDataByteSize / (ch * 2), pos = 0; // 16.16
        while (!q->dead && q->running && (pos >> 16) < frames) {
            const int16_t *s = src + (pos >> 16) * ch;
            out[n * 2] = s[0];
            out[n * 2 + 1] = s[ch - 1];
            pos += step;
            if (++n == AQ_GRAIN) {
                if (q->volume != vol_set && port >= 0) {
                    vol_set = q->volume;
                    int v[2] = {(int)(vol_set * SCE_AUDIO_VOLUME_0DB), (int)(vol_set * SCE_AUDIO_VOLUME_0DB)};
                    sceAudioOutSetVolume(port, SCE_AUDIO_VOLUME_FLAG_L_CH | SCE_AUDIO_VOLUME_FLAG_R_CH, v);
                }
                if (port >= 0)
                    sceAudioOutOutput(port, out);
                else
                    sceKernelDelayThread(AQ_GRAIN * 1000000 / AQ_RATE);
                n = 0;
            }
        }
        if ((pos >> 16) < frames)
            continue; // stopped/paused mid-buffer: it stays first in the FIFO
        pthread_mutex_lock(&q->lock);
        if (q->nfifo && q->fifo[0] == b)
            memmove(q->fifo, q->fifo + 1, --q->nfifo * sizeof(q->fifo[0]));
        pthread_mutex_unlock(&q->lock);
        if (!q->dead && q->running)
            call_guest(q->cb, (uint32_t)q->user, (uint32_t)q, (uint32_t)b, 0);
    }
    if (port >= 0)
        sceAudioOutReleasePort(port);
    for (int i = 0; i < q->nowned; i++) {
        free(q->owned[i]->mAudioData);
        free(q->owned[i]);
    }
    pthread_mutex_destroy(&q->lock);
    free(q);
    return sceKernelExitDeleteThread(0);
}

// AudioQueueNewOutput(&format, callback, userData, runLoop, runLoopMode, flags (stack), &outAQ (stack))
static int aq_new_output(const asbd *fmt, void *cb, void *user, void *rl, void *mode, uint32_t flags, aqueue **out) {
    if (fmt->mFormatID != FOURCC('l', 'p', 'c', 'm') || fmt->mBitsPerChannel != 16 || fmt->mChannelsPerFrame > 2) {
        l_error("[AUDIO] AudioQueueNewOutput: unsupported format '%c%c%c%c' %u bits %u ch", fmt->mFormatID >> 24,
                (fmt->mFormatID >> 16) & 0xFF, (fmt->mFormatID >> 8) & 0xFF, fmt->mFormatID & 0xFF,
                (unsigned)fmt->mBitsPerChannel, (unsigned)fmt->mChannelsPerFrame);
        *out = NULL;
        return FOURCC('f', 'm', 't', '?');
    }
    aqueue *q = calloc(1, sizeof(*q));
    q->fmt = *fmt;
    q->cb = cb;
    q->user = user;
    q->volume = 1.0f;
    q->thread = -1;
    pthread_mutex_init(&q->lock, NULL);
    *out = q;
    return 0;
}

static int aq_allocate_buffer(aqueue *q, uint32_t size, aq_buffer **out) {
    if (!q || q->nowned == AQ_MAX)
        return -1;
    aq_buffer *b = calloc(1, sizeof(*b));
    b->mAudioDataBytesCapacity = size;
    b->mAudioData = calloc(1, size);
    q->owned[q->nowned++] = b;
    *out = b;
    return 0;
}

static int aq_free_buffer(aqueue *q, aq_buffer *b) {
    pthread_mutex_lock(&q->lock);
    for (int i = 0; i < q->nfifo; i++)
        if (q->fifo[i] == b)
            memmove(q->fifo + i, q->fifo + i + 1, (--q->nfifo - i) * sizeof(q->fifo[0]));
    pthread_mutex_unlock(&q->lock);
    return 0; // memory is released with the queue (the audio thread may still be reading it)
}

static int aq_enqueue(aqueue *q, aq_buffer *b, uint32_t npd, void *pd) {
    pthread_mutex_lock(&q->lock);
    int ok = q->nfifo < AQ_MAX;
    if (ok)
        q->fifo[q->nfifo++] = b;
    pthread_mutex_unlock(&q->lock);
    return ok ? 0 : -1;
}

static int aq_start(aqueue *q, void *start_time) {
    q->running = 1;
    if (q->thread < 0) {
        q->thread = sceKernelCreateThread("bgm_queue", aq_thread, 0x10000100 - 20, // above the game (160): slow frames cut the BGM
                                      64 * 1024, 0, 0, NULL);
        if (q->thread < 0 || sceKernelStartThread(q->thread, sizeof(q), &q) < 0) {
            l_error("[AUDIO] AudioQueueStart: thread 0x%08X", q->thread);
            q->thread = -1;
            return -1;
        }
    }
    return 0;
}

static int aq_pause(aqueue *q) {
    q->running = 0;
    return 0;
}

static int aq_stop(aqueue *q, int immediate) {
    q->running = 0;
    if (immediate) {
        pthread_mutex_lock(&q->lock);
        q->nfifo = 0;
        pthread_mutex_unlock(&q->lock);
    }
    return 0;
}

static int aq_dispose(aqueue *q, int immediate) {
    if (!q)
        return 0;
    q->running = 0;
    if (q->thread < 0) { // never started: nothing else references it
        for (int i = 0; i < q->nowned; i++) {
            free(q->owned[i]->mAudioData);
            free(q->owned[i]);
        }
        pthread_mutex_destroy(&q->lock);
        free(q);
        return 0;
    }
    q->dead = 1; // the audio thread frees everything when it sees this
    return 0;
}

// AudioQueueSetParameter(aq, kAudioQueueParam_Volume = 1, Float32 value): softfp, value in r2.
static int aq_set_parameter(aqueue *q, uint32_t id, float v) {
    if (id == 1)
        q->volume = v < 0 ? 0 : v > 1 ? 1 : v;
    return 0;
}

static int aq_set_property(aqueue *q, uint32_t id, const void *data, uint32_t size) { return 0; }

// CFURLCreateFromFileSystemRepresentation(alloc, path bytes, length, isDirectory): NSURL is its
// path string here (objc.c url_with_string), which AudioFileOpenURL reads back.
static id cf_url_from_path(void *alloc, const char *buf, int len, int is_dir) {
    char p[512];
    snprintf(p, sizeof(p), "%.*s", len, buf);
    return ns_string(p);
}

// ---------------------------------------------------------------- OpenAL (openal-soft from VitaSDK)

// Apple extension: buffer data stays owned by the app. openal-soft copies it, which is equivalent
// as long as the game does not rewrite the memory afterwards.
static void al_BufferDataStatic(ALuint buf, ALenum fmt, const ALvoid *data, ALsizei size, ALsizei freq) {
    alBufferData(buf, fmt, data, size, freq);
}

// Extensions are returned from our own import table so guest calls enter through ml_align_call.
static void *al_GetProcAddress(ALCdevice *dev, const ALCchar *name) {
    char n[64];
    snprintf(n, sizeof(n), "_%s", name);
    void *p = (void *)macho_resolve(n);
    l_info("alcGetProcAddress(%s) -> %p", name, p);
    return p;
}

#define AL(name) F(name, name) // openal-soft entry point bound as is (softfp toolchain)

const import_entry coreaudio_imports[] = {
    AL(alBufferData), AL(alGetEnumValue), AL(alIsBuffer), AL(alIsSource), AL(alcGetContextsDevice),
    AL(alcGetCurrentContext), AL(alcGetString),
    F(AudioFileOpenURL, af_open), F(AudioFileClose, af_close), F(AudioFileGetPropertyInfo, af_get_info),
    F(AudioFileGetProperty, af_get), F(AudioFileReadPackets, af_read_packets),
    F(AudioQueueNewOutput, aq_new_output), F(AudioQueueAllocateBuffer, aq_allocate_buffer),
    F(AudioQueueFreeBuffer, aq_free_buffer), F(AudioQueueEnqueueBuffer, aq_enqueue), F(AudioQueueStart, aq_start),
    F(AudioQueuePause, aq_pause), F(AudioQueueStop, aq_stop), F(AudioQueueDispose, aq_dispose),
    F(AudioQueueSetParameter, aq_set_parameter), F(AudioQueueSetProperty, aq_set_property),
    F(CFURLCreateFromFileSystemRepresentation, cf_url_from_path),
    AL(alDeleteBuffers), AL(alDeleteSources), AL(alDistanceModel), AL(alGenBuffers), AL(alGenSources),
    AL(alGetError), AL(alGetSourcei), AL(alListenerfv), AL(alSource3f), AL(alSourcePause), AL(alSourcePlay),
    AL(alSourceQueueBuffers), AL(alSourceStop), AL(alSourceUnqueueBuffers), AL(alSourcef), AL(alSourcefv),
    AL(alSourcei), AL(alcCloseDevice), AL(alcCreateContext), AL(alcDestroyContext),
    AL(alcMakeContextCurrent), AL(alcOpenDevice), AL(alcProcessContext), AL(alcSuspendContext),
    F(alcGetProcAddress, al_GetProcAddress), F(alBufferDataStatic, al_BufferDataStatic),
    F(AudioComponentFindNext, au_FindNext), F(AudioComponentInstanceNew, au_InstanceNew),
    F(AudioComponentInstanceDispose, au_Dispose), F(AudioUnitSetProperty, au_SetProperty),
    F(AudioUnitGetProperty, au_GetProperty), F(AudioUnitInitialize, ok), F(AudioUnitUninitialize, ok),
    F(AudioOutputUnitStart, au_Start), F(AudioOutputUnitStop, au_Stop), F(AudioUnitRender, au_Render),
    F(AudioSessionInitialize, ok), F(AudioSessionSetActive, ok), F(AudioSessionSetProperty, ok),
    F(AudioSessionGetProperty, as_GetProperty), F(AudioSessionAddPropertyListener, ok),
    // imported via __nl_symbol_ptr (function pointer): requires code stub, not data
    F(AudioSessionRemovePropertyListenerWithUserData, ok),
};
const int coreaudio_imports_count = sizeof(coreaudio_imports) / sizeof(coreaudio_imports[0]);
