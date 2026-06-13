#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <android/log.h>

#define TAG "libchams"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, TAG, __VA_ARGS__)

#define EXPORT __attribute__((visibility("default")))

extern "C" {

EXPORT void* __GetModInfo() {
    static const char* info = "libchams|2.0|Chams RxOpenGL|brruham";
    return (void*)info;
}

EXPORT void OnModPreLoad() {
    LOGI("OnModPreLoad");
}

EXPORT void OnModLoad() {
    LOGI("OnModLoad - alive!");
}

}
