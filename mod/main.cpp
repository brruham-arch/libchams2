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

// ─── Base address ───────────────────────────────────────────────────────────
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

// ─── RenderWare function typedefs ────────────────────────────────────────────

// RpSkinGetOpenGLPipeline(RpSkinType) → RxPipeline*
typedef RxPipeline* (*RpSkinGetOpenGLPipeline_t)(RpSkinType);

// RxPipelineFindNodeByName(RxPipeline*, const char*, RxPipelineNode*, int*) → RxPipelineNode*
typedef RxPipelineNode* (*RxPipelineFindNodeByName_t)(
    RxPipeline*, const char*, RxPipelineNode*, int*);

// RxOpenGLAllInOneSetRenderCallBack(RxPipelineNode*, callback) → void
typedef void (*RxOpenGLAllInOneSetRenderCallBack_t)(
    RxPipelineNode*, RxOpenGLAllInOneRenderCB);

// RxOpenGLAllInOneGetRenderCallBack(RxPipelineNode*) → callback
typedef RxOpenGLAllInOneRenderCB (*RxOpenGLAllInOneGetRenderCallBack_t)(
    RxPipelineNode*);

// RwRenderStateSet / Get
typedef int (*RwRenderStateSet_t)(RwRenderState, void*);
typedef int (*RwRenderStateGet_t)(RwRenderState, void*);

// ─── Offsets (confirmed from nm -D libGTASA.so) ──────────────────────────────
// 001c8758 T RpSkinGetOpenGLPipeline
// 001df9a8 T RxPipelineFindNodeByName
// 0022302c T RxOpenGLAllInOneSetRenderCallBack
// 00223032 T RxOpenGLAllInOneGetRenderCallBack
// 001e2914 T RwRenderStateSet
// 001e2948 T RwRenderStateGet

#define OFF_RpSkinGetOpenGLPipeline          0x1c8758
#define OFF_RxPipelineFindNodeByName         0x1df9a8
#define OFF_RxOpenGLAllInOneSetRenderCallBack 0x22302c
#define OFF_RxOpenGLAllInOneGetRenderCallBack 0x223032
#define OFF_RwRenderStateSet                 0x1e2914
#define OFF_RwRenderStateGet                 0x1e2948

// ─── Globals ─────────────────────────────────────────────────────────────────
static RxOpenGLAllInOneRenderCB g_origRenderCB  = nullptr;
static RwRenderStateSet_t       g_RwRSSet        = nullptr;
static RwRenderStateGet_t       g_RwRSGet        = nullptr;

// Chams color: solid red, no texture, visible through walls
// Bisa diganti sesuka hati
static bool g_chamsEnabled = true;

// ─── Custom render callback ───────────────────────────────────────────────────
static void ChamsRenderCB(
    RwResEntry*  repEntry,
    void*        object,
    unsigned char type,
    unsigned int  flags)
{
    if (!g_chamsEnabled || !g_origRenderCB) {
        if (g_origRenderCB) g_origRenderCB(repEntry, object, type, flags);
        return;
    }

    // ── Pass 1: Through-wall (no depth test, solid color) ──
    g_RwRSSet(rwRENDERSTATEZTESTENABLE,   (void*)0);   // disable depth test
    g_RwRSSet(rwRENDERSTATEZWRITEENABLE,  (void*)0);   // disable depth write
    g_RwRSSet(rwRENDERSTATETEXTURERASTER, (void*)0);   // no texture → flat color
    g_RwRSSet(rwRENDERSTATESRCBLEND,      (void*)rwBLENDONE);
    g_RwRSSet(rwRENDERSTATEDESTBLEND,     (void*)rwBLENDZERO);

    // Render through wall (merah)
    g_origRenderCB(repEntry, object, type, flags);

    // ── Pass 2: Normal render on top ──
    g_RwRSSet(rwRENDERSTATEZTESTENABLE,   (void*)1);
    g_RwRSSet(rwRENDERSTATEZWRITEENABLE,  (void*)1);
    g_RwRSSet(rwRENDERSTATETEXTURERASTER, (void*)0);   // tetap no texture
    g_RwRSSet(rwRENDERSTATESRCBLEND,      (void*)rwBLENDSRCALPHA);
    g_RwRSSet(rwRENDERSTATEDESTBLEND,     (void*)rwBLENDINVSRCALPHA);

    g_origRenderCB(repEntry, object, type, flags);

    // ── Restore default state ──
    g_RwRSSet(rwRENDERSTATEZTESTENABLE,   (void*)1);
    g_RwRSSet(rwRENDERSTATEZWRITEENABLE,  (void*)1);
    g_RwRSSet(rwRENDERSTATESRCBLEND,      (void*)rwBLENDSRCALPHA);
    g_RwRSSet(rwRENDERSTATEDESTBLEND,     (void*)rwBLENDINVSRCALPHA);
}

// ─── Install hook ─────────────────────────────────────────────────────────────
static bool InstallChams() {
    uintptr_t base = GetBase();
    if (!base) { LOGE("Failed to get base"); return false; }

    auto fnSkinGetPipe = (RpSkinGetOpenGLPipeline_t)          OFF(OFF_RpSkinGetOpenGLPipeline);
    auto fnFindNode    = (RxPipelineFindNodeByName_t)          OFF(OFF_RxPipelineFindNodeByName);
    auto fnSetCB       = (RxOpenGLAllInOneSetRenderCallBack_t) OFF(OFF_RxOpenGLAllInOneSetRenderCallBack);
    auto fnGetCB       = (RxOpenGLAllInOneGetRenderCallBack_t) OFF(OFF_RxOpenGLAllInOneGetRenderCallBack);

    g_RwRSSet = (RwRenderStateSet_t) OFF(OFF_RwRenderStateSet);
    g_RwRSGet = (RwRenderStateGet_t) OFF(OFF_RwRenderStateGet);

    // Ambil skin pipeline (MATFX = ped pakai MatFX pipeline)
    RxPipeline* pipe = fnSkinGetPipe(rpSKINTYPEMATFX);
    if (!pipe) {
        // Fallback ke generic
        pipe = fnSkinGetPipe(rpSKINTYPEGENERIC);
    }
    if (!pipe) { LOGE("Failed to get skin pipeline"); return false; }
    LOGI("Skin pipeline: %p", pipe);

    // Cari node render
    int nodeIdx = 0;
    RxPipelineNode* node = fnFindNode(pipe, "OpenGLAtomicAllInOne", nullptr, &nodeIdx);
    if (!node) { LOGE("Failed to find pipeline node"); return false; }
    LOGI("Pipeline node: %p (idx %d)", node, nodeIdx);

    // Simpan original callback
    g_origRenderCB = fnGetCB(node);
    if (!g_origRenderCB) { LOGE("Failed to get original render CB"); return false; }
    LOGI("Original CB: %p", g_origRenderCB);

    // Set callback kita
    fnSetCB(node, ChamsRenderCB);
    LOGI("Chams render CB installed!");

    return true;
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
    info->version    = "2.0";
    info->handlerVer = 1;
}

extern "C" __attribute__((visibility("default")))
void OnModPreLoad() {
    LOGI("OnModPreLoad");
}

extern "C" __attribute__((visibility("default")))
void OnModLoad() {
    LOGI("OnModLoad - installing chams...");
    if (InstallChams()) {
        LOGI("Chams v2.0 loaded successfully");
    } else {
        LOGE("Chams install failed");
    }
}
