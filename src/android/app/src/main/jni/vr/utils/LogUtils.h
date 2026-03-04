/*******************************************************************************

Filename    :   LogUtils.h

Content     :   Logging macros I define in every project, with Citra backend

Authors     :   Amanda M. Watson
License     :   Licensed under GPLv3 or any later version.
                Refer to the license.txt file included.

*******************************************************************************/
#pragma once

#include "common/logging/log.h"

#include <android/log.h>

#include <stdlib.h>

#ifndef LOG_TAG
#define LOG_TAG "Citra::Input"
#endif

#define ALOGE(...) LOG_ERROR(VR, __VA_ARGS__)
#define ALOGW(...) LOG_WARNING(VR, __VA_ARGS__)
#define ALOGI(...) LOG_INFO(VR, __VA_ARGS__)
#define ALOGV(...) LOG_TRACE(VR, __VA_ARGS__)
#define ALOGD(...) LOG_DEBUG(VR, __VA_ARGS__)

#define CITRAVR_XR_TAG   "CITRAVR_XR"
#define CITRAVR_PORT_TAG "CITRAVR_PORT"
#define XR_PORT_LOGI(...) __android_log_print(ANDROID_LOG_INFO, CITRAVR_PORT_TAG, __VA_ARGS__)
#define XR_PORT_LOGW(...) __android_log_print(ANDROID_LOG_WARN, CITRAVR_PORT_TAG, __VA_ARGS__)
#define XR_PORT_LOGE(...) __android_log_print(ANDROID_LOG_ERROR, CITRAVR_PORT_TAG, __VA_ARGS__)
#define XR_DIAG_LOGI(...) __android_log_print(ANDROID_LOG_INFO, CITRAVR_XR_TAG, __VA_ARGS__)
#define XR_DIAG_LOGW(...) __android_log_print(ANDROID_LOG_WARN, CITRAVR_XR_TAG, __VA_ARGS__)
#define XR_DIAG_LOGE(...) __android_log_print(ANDROID_LOG_ERROR, CITRAVR_XR_TAG, __VA_ARGS__)

// Use these for logging before logging system is initialized
#define ANDROID_ONLY_LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)
#define ANDROID_ONLY_LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)
#define ANDROID_ONLY_LOGW(...) __android_log_print(ANDROID_LOG_WARN, LOG_TAG, __VA_ARGS__)

#define FAIL(...)                                                                                  \
    do {                                                                                           \
        __android_log_print(ANDROID_LOG_FATAL, LOG_TAG, __VA_ARGS__);                              \
        abort();                                                                                   \
    } while (0)
