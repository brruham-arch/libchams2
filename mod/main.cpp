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

// ─── Typedefs ─────────────────────────────────────────────────────────────────
typedef RxPipeline*              (*RpSkinGetOpenGLPipeline_t)(RpSkinType);
typedef RxPipelineNode*          (*RxPipelineFindNodeByName_t)(RxPipeline*, const char*, RxPipelineNode*, int*);
typedef void                     (*RxOpenGLAllInOneSetRenderCallBack_t)(RxPipelineNode*, RxOpenGLAllInOneRenderCB);
typedef RxOpenGLAllInOneRenderCB (*RxOpenGLAllInOneGetRenderCallBack_t)(RxPipelineNode*);
typedef int                      (*RwRenderStateSet_t)(RwRenderState, void*);
typedef int                      (*DobbyHook_t)(void*, void*, void**);

// ─── Offsets ──────────────────────────────────────────────────────────────────
#define OFF_RpSkinGetOpenGLPipeline           0x1c8758
#define OFF_RxPipelineFindNodeByName          0x1df9a8
#define OFF_RxOpenGLAllInOneSetRenderCallBack 0x22302c
#define OFF_RxOpenGLAllInOneGetRenderCallBack 0x223032
#define OFF_RwRenderStateSet                  0x1e2914

// ─── Globals ──────────────────────────────────────────────────────────────────
static RpSkinGetOpenGLPipeline_t        g_origSkinGetPipe = nullptr;
static RxOpenGLAllInOneRenderCB         g_origRenderCB    = nullptr;
static RwRenderStateSet_t               g_RwRSSet         = nullptr;
static bool                             g_chamsInstalled  = false;

// ─── Chams render callback ────────────────────────────────────────────────────
static void ChamsRenderCB(RwResEntry* repEntry, void* object,
                          unsigned char type, unsigned int flags) {
    if (!g_origRenderCB) return;

    // Pass 1: through wall, no texture
    g_RwRSSet(rwRENDERSTATEZTESTENABLE,   (void*)0);
    g_RwRSSet(rwRENDERSTATEZWRITEENABLE,  (void*)0);
    g_RwRSSet(rwRENDERSTATETEXTURERASTER, (void*)0);
    g_origRenderCB(repEntry, object, type, flags);

    // Pass 2: normal on top
    g_RwRSSet(rwRENDERSTATEZTESTENABLE,   (void*)1);
    g_RwRSSet(rwRENDERSTATEZWRITEENABLE,  (void*)1);
    g_origRenderCB(repEntry, object, type, flags);

    // Restore
    g_RwRSSet(rwRENDERSTATEZTESTENABLE,   (void*)1);
    g_RwRSSet(rwRENDERSTATEZWRITEENABLE,  (void*)1);
    g_RwRSSet(rwRENDERSTATETEXTURERASTER, (void*)0);
}

// ─── Hook: RpSkinGetOpenGLPipeline ───────────────────────────────────────────
// Dipanggil game saat init pipeline — kita intercept dan install chams
static RxPipeline* hook_RpSkinGetOpenGLPipeline(RpSkinType type) {
    RxPipeline* pipe = g_origSkinGetPipe(type);

    LOGF("[chams] hook_RpSkinGetOpenGLPipeline type=%d pipe=%p", (int)type, pipe);

    if (!g_chamsInstalled && pipe) {
        auto fnFindNode = (RxPipelineFindNodeByName_t) OFF(OFF_RxPipelineFindNodeByName);
        auto fnSetCB    = (RxOpenGLAllInOneSetRenderCallBack_t) OFF(OFF_RxOpenGLAllInOneSetRenderCallBack);
        auto fnGetCB    = (RxOpenGLAllInOneGetRenderCallBack_t) OFF(OFF_RxOpenGLAllInOneGetRenderCallBack);

        int idx = 0;
        RxPipelineNode* node = fnFindNode(pipe, "OpenGLAtomicAllInOne", nullptr, &idx);
        LOGF("[chams] node=%p idx=%d", node, idx);

        if (node) {
            g_origRenderCB = fnGetCB(node);
            LOGF("[chams] origCB=%p", g_origRenderCB);

            if (g_origRenderCB) {
                fnSetCB(node, ChamsRenderCB);
                g_chamsInstalled = true;
                LOGF("[chams] SUCCESS installed on type=%d!", (int)type);
            }
        }
    }

    return pipe;
}

// ─── AML exports ──────────────────────────────────────────────────────────────
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

    // Load Dobby
    void* hDobby = dlopen("libdobby.so", RTLD_NOW | RTLD_GLOBAL);
    if (!hDobby) { LOGF("[chams] ERROR: libdobby: %s", dlerror()); return; }
    LOGF("[chams] Dobby loaded");

    auto DobbyHook = (DobbyHook_t)dlsym(hDobby, "DobbyHook");
    if (!DobbyHook) { LOGF("[chams] ERROR: DobbyHook sym"); return; }
    LOGF("[chams] DobbyHook: %p", DobbyHook);

    // Hook RpSkinGetOpenGLPipeline — tunggu game panggil sendiri
    void* addr = (void*)OFF(OFF_RpSkinGetOpenGLPipeline);
    LOGF("[chams] Hooking RpSkinGetOpenGLPipeline @ %p", addr);

    int ret = DobbyHook(addr, (void*)hook_RpSkinGetOpenGLPipeline, (void**)&g_origSkinGetPipe);
    LOGF("[chams] DobbyHook ret=%d origPipe=%p", ret, g_origSkinGetPipe);

    if (ret != 0) { LOGF("[chams] ERROR: DobbyHook failed"); return; }
    LOGF("[chams] OnModLoad done - waiting for pipeline init...");
}

}
