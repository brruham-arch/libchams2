#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <dlfcn.h>
#include <android/log.h>
#include "rw_types.h"

#define TAG "libchams"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO,  TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, TAG, __VA_ARGS__)

// ─── Base address ────────────────────────────────────────────────────────────
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
    LOGI("Base: 0x%08X", (uint32_t)g_base);
    return g_base;
}

#define OFF(x) (g_base + (x))

// ─── Typedefs ────────────────────────────────────────────────────────────────
typedef RxPipeline* (*RpSkinGetOpenGLPipeline_t)(RpSkinType);
typedef RxPipelineNode* (*RxPipelineFindNodeByName_t)(RxPipeline*, const char*, RxPipelineNode*, int*);
typedef void (*RxOpenGLAllInOneSetRenderCallBack_t)(RxPipelineNode*, RxOpenGLAllInOneRenderCB);
typedef RxOpenGLAllInOneRenderCB (*RxOpenGLAllInOneGetRenderCallBack_t)(RxPipelineNode*);
typedef int  (*RwRenderStateSet_t)(RwRenderState, void*);
typedef int  (*RwRenderStateGet_t)(RwRenderState, void*);
typedef void* (*eglSwapBuffers_t)(void*, void*);

// ─── Offsets ─────────────────────────────────────────────────────────────────
#define OFF_RpSkinGetOpenGLPipeline           0x1c8758
#define OFF_RxPipelineFindNodeByName          0x1df9a8
#define OFF_RxOpenGLAllInOneSetRenderCallBack 0x22302c
#define OFF_RxOpenGLAllInOneGetRenderCallBack 0x223032
#define OFF_RwRenderStateSet                  0x1e2914
#define OFF_RwRenderStateGet                  0x1e2948
#define OFF_eglSwapBuffers                    0x268f4c

// ─── Globals ─────────────────────────────────────────────────────────────────
static RxOpenGLAllInOneRenderCB g_origRenderCB = nullptr;
static RwRenderStateSet_t       g_RwRSSet       = nullptr;
static eglSwapBuffers_t         g_origSwap      = nullptr;
static bool                     g_chamsInstalled = false;

// ─── Dobby ───────────────────────────────────────────────────────────────────
typedef int (*DobbyHook_t)(void* addr, void* replace, void** orig);
static DobbyHook_t DobbyHook = nullptr;

static bool LoadDobby() {
    void* h = dlopen("libdobby.so", RTLD_NOW);
    if (!h) { LOGE("dlopen libdobby.so failed: %s", dlerror()); return false; }
    DobbyHook = (DobbyHook_t)dlsym(h, "DobbyHook");
    if (!DobbyHook) { LOGE("dlsym DobbyHook failed"); return false; }
    LOGI("Dobby loaded");
    return true;
}

// ─── Chams render callback ────────────────────────────────────────────────────
static void ChamsRenderCB(RwResEntry* repEntry, void* object, unsigned char type, unsigned int flags) {
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

// ─── Install chams (dipanggil dari eglSwapBuffers hook) ───────────────────────
static bool InstallChams() {
    uintptr_t base = GetBase();
    if (!base) { LOGE("No base"); return false; }

    auto fnSkinGetPipe = (RpSkinGetOpenGLPipeline_t)          OFF(OFF_RpSkinGetOpenGLPipeline);
    auto fnFindNode    = (RxPipelineFindNodeByName_t)          OFF(OFF_RxPipelineFindNodeByName);
    auto fnSetCB       = (RxOpenGLAllInOneSetRenderCallBack_t) OFF(OFF_RxOpenGLAllInOneSetRenderCallBack);
    auto fnGetCB       = (RxOpenGLAllInOneGetRenderCallBack_t) OFF(OFF_RxOpenGLAllInOneGetRenderCallBack);
    g_RwRSSet          = (RwRenderStateSet_t)                  OFF(OFF_RwRenderStateSet);

    // Coba MATFX dulu, fallback GENERIC
    RxPipeline* pipe = fnSkinGetPipe(rpSKINTYPEMATFX);
    if (!pipe) pipe  = fnSkinGetPipe(rpSKINTYPEGENERIC);
    if (!pipe) { LOGE("No skin pipeline"); return false; }
    LOGI("Pipe: %p", pipe);

    int idx = 0;
    RxPipelineNode* node = fnFindNode(pipe, "OpenGLAtomicAllInOne", nullptr, &idx);
    if (!node) { LOGE("Node not found"); return false; }
    LOGI("Node: %p", node);

    g_origRenderCB = fnGetCB(node);
    if (!g_origRenderCB) { LOGE("No orig CB"); return false; }
    LOGI("OrigCB: %p", g_origRenderCB);

    fnSetCB(node, ChamsRenderCB);
    LOGI("Chams installed!");
    return true;
}

// ─── eglSwapBuffers hook ──────────────────────────────────────────────────────
// Dipanggil setiap frame — tunggu sampai game fully loaded baru install chams
static int g_swapCount = 0;

static void* eglSwapBuffers_hook(void* display, void* surface) {
    if (!g_chamsInstalled) {
        g_swapCount++;
        // Tunggu 120 frame (~2 detik) biar RenderWare fully init
        if (g_swapCount >= 120) {
            g_chamsInstalled = InstallChams();
            if (!g_chamsInstalled) {
                LOGE("InstallChams failed at frame %d, retrying next frame...", g_swapCount);
                g_swapCount = 119; // retry tiap frame
            }
        }
    }
    return g_origSwap(display, surface);
}

// ─── AML exports ─────────────────────────────────────────────────────────────
struct ModInfo {
    const char* name;
    const char* version;
    int         handlerVer;
};

extern "C" __attribute__((visibility("default")))
void __GetModInfo(ModInfo* info) {
    info->name       = "libchams";
    info->version    = "2.1";
    info->handlerVer = 1;
}

extern "C" __attribute__((visibility("default")))
void OnModPreLoad() {
    LOGI("OnModPreLoad");
}

extern "C" __attribute__((visibility("default")))
void OnModLoad() {
    LOGI("OnModLoad");

    if (!LoadDobby()) return;

    uintptr_t base = GetBase();
    if (!base) { LOGE("No base in OnModLoad"); return; }

    // Hook eglSwapBuffers untuk delay install
    void* addrSwap = (void*)OFF(OFF_eglSwapBuffers);
    int ret = DobbyHook(addrSwap, (void*)eglSwapBuffers_hook, (void**)&g_origSwap);
    if (ret != 0) {
        LOGE("DobbyHook eglSwapBuffers failed: %d", ret);
        return;
    }
    LOGI("eglSwapBuffers hooked, waiting for game init...");
}
