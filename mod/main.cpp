#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <dlfcn.h>
#include <android/log.h>
#include "rw_types.h"

#define TAG     "libchams"
#define LOGFILE "/storage/emulated/0/chams_log.txt"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, TAG, __VA_ARGS__)
static void logf_impl(const char* msg) {
    FILE* f = fopen(LOGFILE, "a");
    if (f) { fprintf(f, "%s\n", msg); fclose(f); }
    LOGI("%s", msg);
}
#define LOGF(fmt,...) do{ char _b[512]; snprintf(_b,sizeof(_b),fmt,##__VA_ARGS__); logf_impl(_b); }while(0)
#define EXPORT __attribute__((visibility("default")))

static uintptr_t g_base = 0;
static uintptr_t GetBase() {
    if (g_base) return g_base;
    FILE* f = fopen("/proc/self/maps", "r");
    if (!f) return 0;
    char line[256];
    while (fgets(line, sizeof(line), f)) {
        if (strstr(line, "libGTASA.so") && strstr(line, "r-xp")) {
            g_base = (uintptr_t)strtoul(line, nullptr, 16);
            break;
        }
    }
    fclose(f);
    return g_base;
}
#define OFF(x) (g_base + (x))

typedef RxPipeline*              (*RpSkinGetOpenGLPipeline_t)(RpSkinType);
typedef RxPipelineNode*          (*RxPipelineFindNodeByName_t)(RxPipeline*, const char*, RxPipelineNode*, int*);
typedef void                     (*RxOpenGLAllInOneSetRenderCallBack_t)(RxPipelineNode*, RxOpenGLAllInOneRenderCB);
typedef RxOpenGLAllInOneRenderCB (*RxOpenGLAllInOneGetRenderCallBack_t)(RxPipelineNode*);
typedef int                      (*RwRenderStateSet_t)(RwRenderState, void*);
typedef int                      (*DobbyHook_t)(void*, void*, void**);

#define OFF_RpSkinGetOpenGLPipeline           0x1c8758
#define OFF_RxPipelineFindNodeByName          0x1df9a8
#define OFF_RxOpenGLAllInOneSetRenderCallBack 0x22302c
#define OFF_RxOpenGLAllInOneGetRenderCallBack 0x223032
#define OFF_RwRenderStateSet                  0x1e2914

static RxOpenGLAllInOneRenderCB g_origRenderCB = nullptr;
static RwRenderStateSet_t       g_RwRSSet       = nullptr;

static void ChamsRenderCB(RwResEntry* repEntry, void* object,
                          unsigned char type, unsigned int flags) {
    if (!g_origRenderCB) return;
    // Pass 1: through wall
    g_RwRSSet(rwRENDERSTATEZTESTENABLE,   (void*)0);
    g_RwRSSet(rwRENDERSTATEZWRITEENABLE,  (void*)0);
    g_RwRSSet(rwRENDERSTATETEXTURERASTER, (void*)0);
    g_origRenderCB(repEntry, object, type, flags);
    // Pass 2: normal
    g_RwRSSet(rwRENDERSTATEZTESTENABLE,   (void*)1);
    g_RwRSSet(rwRENDERSTATEZWRITEENABLE,  (void*)1);
    g_origRenderCB(repEntry, object, type, flags);
    // Restore
    g_RwRSSet(rwRENDERSTATEZTESTENABLE,   (void*)1);
    g_RwRSSet(rwRENDERSTATEZWRITEENABLE,  (void*)1);
    g_RwRSSet(rwRENDERSTATETEXTURERASTER, (void*)0);
}

static void TryInstallOnPipe(RxPipeline* pipe, const char* label) {
    if (!pipe) { LOGF("[chams] TryInstall: null pipe (%s)", label); return; }
    LOGF("[chams] TryInstall pipe=%p (%s)", pipe, label);

    auto fnFindNode = (RxPipelineFindNodeByName_t)          OFF(OFF_RxPipelineFindNodeByName);
    auto fnSetCB    = (RxOpenGLAllInOneSetRenderCallBack_t) OFF(OFF_RxOpenGLAllInOneSetRenderCallBack);
    auto fnGetCB    = (RxOpenGLAllInOneGetRenderCallBack_t) OFF(OFF_RxOpenGLAllInOneGetRenderCallBack);

    int idx = 0;
    RxPipelineNode* node = fnFindNode(pipe, "OpenGLAtomicAllInOne", nullptr, &idx);
    LOGF("[chams] node=%p idx=%d", node, idx);
    if (!node) { LOGF("[chams] node not found (%s)", label); return; }

    g_origRenderCB = fnGetCB(node);
    LOGF("[chams] origCB=%p", g_origRenderCB);
    if (!g_origRenderCB) { LOGF("[chams] no origCB (%s)", label); return; }

    fnSetCB(node, ChamsRenderCB);
    LOGF("[chams] SUCCESS on %s!", label);
}

extern "C" {

EXPORT void* __GetModInfo() {
    static const char* info = "libchams|2.0|Chams RxOpenGL|brruham";
    return (void*)info;
}

EXPORT void OnModPreLoad() {
    remove(LOGFILE);
    LOGF("[chams] OnModPreLoad");
}

EXPORT void OnModLoad() {
    LOGF("[chams] OnModLoad start");

    uintptr_t base = GetBase();
    if (!base) { LOGF("[chams] ERROR: no base"); return; }
    LOGF("[chams] base: 0x%08X", (uint32_t)base);

    g_RwRSSet = (RwRenderStateSet_t) OFF(OFF_RwRenderStateSet);

    // Panggil langsung — pipeline sudah init sebelum mod load
    auto fnSkinGetPipe = (RpSkinGetOpenGLPipeline_t) OFF(OFF_RpSkinGetOpenGLPipeline);

    // Coba semua type
    for (int t = 0; t <= 3; t++) {
        LOGF("[chams] trying type=%d", t);
        RxPipeline* pipe = fnSkinGetPipe((RpSkinType)t);
        LOGF("[chams] type=%d pipe=%p", t, pipe);
        if (pipe) {
            TryInstallOnPipe(pipe, t == 0 ? "NASKIN" :
                                   t == 1 ? "GENERIC" :
                                   t == 2 ? "MATFX" : "TOON");
            if (g_origRenderCB) break;
        }
    }

    LOGF("[chams] OnModLoad done origCB=%p", g_origRenderCB);
}

}
