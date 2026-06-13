#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <android/log.h>

#define TAG "libchams"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO,  TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, TAG, __VA_ARGS__)

struct ModInfo {
    const char* name;
    const char* version;
    int         handlerVer;
};

extern "C" __attribute__((visibility("default")))
void __GetModInfo(ModInfo* info) {
    info->name       = "libchams";
    info->version    = "2.2-test";
    info->handlerVer = 1;
}

extern "C" __attribute__((visibility("default")))
void OnModPreLoad() {
    LOGI("OnModPreLoad called");
}

extern "C" __attribute__((visibility("default")))
void OnModLoad() {
    LOGI("OnModLoad called - mod is alive!");
}
