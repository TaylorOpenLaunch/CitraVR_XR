// Copyright 2019 Citra Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include <algorithm>
#include <array>
#include <cstdlib>
#include <string>
#include <android/native_window_jni.h>
#include <android/log.h>
#include "common/logging/log.h"
#include "common/settings.h"
#include "input_common/main.h"
#include "jni/emu_window/emu_window.h"
#include "jni/id_cache.h"
#include "jni/input_manager.h"
#include "network/network.h"
#include "video_core/renderer_base.h"

static bool IsPortraitMode() {
    return JNI_FALSE != IDCache::GetEnvForThread()->CallStaticBooleanMethod(
                            IDCache::GetNativeLibraryClass(), IDCache::GetIsPortraitMode());
}

static void UpdateLandscapeScreenLayout() {
    Settings::values.layout_option =
        static_cast<Settings::LayoutOption>(IDCache::GetEnvForThread()->CallStaticIntMethod(
            IDCache::GetNativeLibraryClass(), IDCache::GetLandscapeScreenLayout()));
}

void EmuWindow_Android::OnSurfaceChanged(ANativeWindow* surface, bool xr_surface) {
    render_window = surface;
    is_xr_surface = xr_surface;

    if (surface) {
        window_width = ANativeWindow_getWidth(surface);
        window_height = ANativeWindow_getHeight(surface);
    } else {
        window_width = 0;
        window_height = 0;
    }

    window_info.type = Frontend::WindowSystemType::Android;
    window_info.render_surface = surface;

    __android_log_print(ANDROID_LOG_INFO, "CITRAVR_PORT",
                        "OnSurfaceChanged: surface=%p xr_surface=%d size=%dx%d",
                        static_cast<void*>(surface), xr_surface ? 1 : 0, window_width, window_height);

    StopPresenting();
    OnFramebufferSizeChanged();
}

bool EmuWindow_Android::OnTouchEvent(int x, int y, bool pressed) {
    if (pressed) {
        return TouchPressed((unsigned)std::max(x, 0), (unsigned)std::max(y, 0));
    }

    TouchReleased();
    return true;
}

void EmuWindow_Android::OnTouchMoved(int x, int y) {
    TouchMoved((unsigned)std::max(x, 0), (unsigned)std::max(y, 0));
}

void EmuWindow_Android::OnFramebufferSizeChanged() {
    UpdateLandscapeScreenLayout();
    const bool is_portrait_mode{IsPortraitMode()};

    const int bigger{window_width > window_height ? window_width : window_height};
    const int smaller{window_width < window_height ? window_width : window_height};
    if (is_portrait_mode) {
        UpdateCurrentFramebufferLayout(smaller, bigger, is_portrait_mode);
    } else {
        UpdateCurrentFramebufferLayout(bigger, smaller, is_portrait_mode);
    }
}

EmuWindow_Android::EmuWindow_Android(ANativeWindow* surface, bool xr_surface)
    : host_window{surface}, is_xr_surface{xr_surface} {
    LOG_DEBUG(Frontend, "Initializing EmuWindow_Android");

    if (!surface) {
        LOG_CRITICAL(Frontend, "surface is nullptr");
        return;
    }

    window_width = ANativeWindow_getWidth(surface);
    window_height = ANativeWindow_getHeight(surface);
    __android_log_print(ANDROID_LOG_INFO, "CITRAVR_PORT",
                        "EmuWindow_Android ctor: surface=%p xr_surface=%d size=%dx%d",
                        static_cast<void*>(surface), xr_surface ? 1 : 0, window_width, window_height);

    Network::Init();
}

EmuWindow_Android::~EmuWindow_Android() {
    DestroyWindowSurface();
    DestroyContext();
}

void EmuWindow_Android::MakeCurrent() {
    core_context->MakeCurrent();
}

void EmuWindow_Android::DoneCurrent() {
    core_context->DoneCurrent();
}
