LOCAL_PATH := $(call my-dir)

include $(CLEAR_VARS)

LOCAL_MODULE    := chams
LOCAL_SRC_FILES := ../mod/main.cpp
LOCAL_CPPFLAGS  := -std=c++17 -O2 -fvisibility=hidden
LOCAL_LDLIBS    := -llog -landroid

include $(BUILD_SHARED_LIBRARY)
