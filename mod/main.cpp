#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <android/log.h>

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
    LOGF("[chams] base: 0x%08X", (uint32_t)base);

    // Hanya log alamat, TIDAK memanggil fungsi apapun
    LOGF("[chams] RpSkinGetOpenGLPipeline addr: 0x%08X", (uint32_t)(base + 0x1c8758));
    LOGF("[chams] RxPipelineFindNodeByName  addr: 0x%08X", (uint32_t)(base + 0x1df9a8));
    LOGF("[chams] RxOpenGLAllInOneSetRenderCB addr: 0x%08X", (uint32_t)(base + 0x22302c));
    LOGF("[chams] RwRenderStateSet addr: 0x%08X", (uint32_t)(base + 0x1e2914));

    LOGF("[chams] OnModLoad done - no crash");
}

}
