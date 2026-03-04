// Copyright 2019 Citra Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include <algorithm>
#include <array>
#include <cstdlib>
#include <string>

#include <android/log.h>
#include <android/native_window_jni.h>
#include <glad/glad.h>

#include "common/logging/log.h"
#include "common/settings.h"
#include "core/core.h"
#include "input_common/main.h"
#include "jni/emu_window/emu_window_gl.h"
#include "video_core/gpu.h"
#include "video_core/renderer_base.h"

namespace {

const char* EglErrorToString(EGLint error) {
    switch (error) {
    case EGL_SUCCESS:
        return "EGL_SUCCESS";
    case EGL_NOT_INITIALIZED:
        return "EGL_NOT_INITIALIZED";
    case EGL_BAD_ACCESS:
        return "EGL_BAD_ACCESS";
    case EGL_BAD_ALLOC:
        return "EGL_BAD_ALLOC";
    case EGL_BAD_ATTRIBUTE:
        return "EGL_BAD_ATTRIBUTE";
    case EGL_BAD_CONTEXT:
        return "EGL_BAD_CONTEXT";
    case EGL_BAD_CONFIG:
        return "EGL_BAD_CONFIG";
    case EGL_BAD_CURRENT_SURFACE:
        return "EGL_BAD_CURRENT_SURFACE";
    case EGL_BAD_DISPLAY:
        return "EGL_BAD_DISPLAY";
    case EGL_BAD_SURFACE:
        return "EGL_BAD_SURFACE";
    case EGL_BAD_MATCH:
        return "EGL_BAD_MATCH";
    case EGL_BAD_PARAMETER:
        return "EGL_BAD_PARAMETER";
    case EGL_BAD_NATIVE_PIXMAP:
        return "EGL_BAD_NATIVE_PIXMAP";
    case EGL_BAD_NATIVE_WINDOW:
        return "EGL_BAD_NATIVE_WINDOW";
    case EGL_CONTEXT_LOST:
        return "EGL_CONTEXT_LOST";
    default:
        return "EGL_UNKNOWN";
    }
}

} // namespace

static constexpr std::array<EGLint, 15> egl_attribs{EGL_SURFACE_TYPE,
                                                    EGL_WINDOW_BIT,
                                                    EGL_RENDERABLE_TYPE,
                                                    EGL_OPENGL_ES3_BIT_KHR,
                                                    EGL_BLUE_SIZE,
                                                    8,
                                                    EGL_GREEN_SIZE,
                                                    8,
                                                    EGL_RED_SIZE,
                                                    8,
                                                    EGL_DEPTH_SIZE,
                                                    0,
                                                    EGL_STENCIL_SIZE,
                                                    0,
                                                    EGL_NONE};
static constexpr std::array<EGLint, 5> egl_empty_attribs{EGL_WIDTH, 1, EGL_HEIGHT, 1, EGL_NONE};
static constexpr std::array<EGLint, 4> egl_context_attribs{EGL_CONTEXT_CLIENT_VERSION, 3, EGL_NONE};

class SharedContext_Android : public Frontend::GraphicsContext {
public:
    SharedContext_Android(EGLDisplay egl_display, EGLConfig egl_config,
                          EGLContext egl_share_context)
        : egl_display{egl_display}, egl_surface{eglCreatePbufferSurface(egl_display, egl_config,
                                                                        egl_empty_attribs.data())},
          egl_context{eglCreateContext(egl_display, egl_config, egl_share_context,
                                       egl_context_attribs.data())} {
        ASSERT_MSG(egl_surface, "eglCreatePbufferSurface() failed!");
        ASSERT_MSG(egl_context, "eglCreateContext() failed!");
    }

    ~SharedContext_Android() override {
        if (!eglDestroySurface(egl_display, egl_surface)) {
            LOG_CRITICAL(Frontend, "eglDestroySurface() failed");
        }

        if (!eglDestroyContext(egl_display, egl_context)) {
            LOG_CRITICAL(Frontend, "eglDestroySurface() failed");
        }
    }

    void MakeCurrent() override {
        eglMakeCurrent(egl_display, egl_surface, egl_surface, egl_context);
    }

    void DoneCurrent() override {
        eglMakeCurrent(egl_display, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
    }

private:
    EGLDisplay egl_display{};
    EGLSurface egl_surface{};
    EGLContext egl_context{};
};

EmuWindow_Android_OpenGL::EmuWindow_Android_OpenGL(Core::System& system_, ANativeWindow* surface,
                                                   bool is_xr_surface)
    : EmuWindow_Android{surface, is_xr_surface}, system{system_} {
    __android_log_print(ANDROID_LOG_INFO, "CITRAVR_PORT",
                        "OpenGL window ctor: xr_surface=%d initial_size=%dx%d",
                        IsXrSurface() ? 1 : 0, window_width, window_height);
    if (egl_display = eglGetDisplay(EGL_DEFAULT_DISPLAY); egl_display == EGL_NO_DISPLAY) {
        LOG_CRITICAL(Frontend, "eglGetDisplay() failed");
        return;
    }
    if (eglInitialize(egl_display, 0, 0) != EGL_TRUE) {
        LOG_CRITICAL(Frontend, "eglInitialize() failed");
        return;
    }
    if (EGLint egl_num_configs{}; eglChooseConfig(egl_display, egl_attribs.data(), &egl_config, 1,
                                                  &egl_num_configs) != EGL_TRUE) {
        LOG_CRITICAL(Frontend, "eglChooseConfig() failed");
        return;
    }

    CreateWindowSurface();

    if (eglQuerySurface(egl_display, egl_surface, EGL_WIDTH, &window_width) != EGL_TRUE) {
        return;
    }
    if (eglQuerySurface(egl_display, egl_surface, EGL_HEIGHT, &window_height) != EGL_TRUE) {
        return;
    }

    if (egl_context = eglCreateContext(egl_display, egl_config, 0, egl_context_attribs.data());
        egl_context == EGL_NO_CONTEXT) {
        LOG_CRITICAL(Frontend, "eglCreateContext() failed");
        return;
    }
    if (eglSurfaceAttrib(egl_display, egl_surface, EGL_SWAP_BEHAVIOR, EGL_BUFFER_DESTROYED) !=
        EGL_TRUE) {
        LOG_CRITICAL(Frontend, "eglSurfaceAttrib() failed");
        return;
    }
    if (core_context = CreateSharedContext(); !core_context) {
        LOG_CRITICAL(Frontend, "CreateSharedContext() failed");
        return;
    }
    if (eglMakeCurrent(egl_display, egl_surface, egl_surface, egl_context) != EGL_TRUE) {
        LOG_CRITICAL(Frontend, "eglMakeCurrent() failed");
        return;
    }
    if (!gladLoadGLES2Loader((GLADloadproc)eglGetProcAddress)) {
        LOG_CRITICAL(Frontend, "gladLoadGLES2Loader() failed");
        return;
    }
    if (!eglSwapInterval(egl_display, Settings::values.use_vsync_new ? 1 : 0)) {
        LOG_CRITICAL(Frontend, "eglSwapInterval() failed");
        return;
    }

    OnFramebufferSizeChanged();
}

bool EmuWindow_Android_OpenGL::CreateWindowSurface() {
    if (!host_window) {
        return true;
    }

    const int native_width = ANativeWindow_getWidth(host_window);
    const int native_height = ANativeWindow_getHeight(host_window);
    __android_log_print(ANDROID_LOG_INFO, "CITRAVR_PORT",
                        "CreateWindowSurface: xr_surface=%d native_size=%dx%d",
                        IsXrSurface() ? 1 : 0, native_width, native_height);

    EGLint format{};
    eglGetConfigAttrib(egl_display, egl_config, EGL_NATIVE_VISUAL_ID, &format);
    const int geometry_result = ANativeWindow_setBuffersGeometry(host_window, 0, 0, format);
    __android_log_print(ANDROID_LOG_INFO, "CITRAVR_PORT",
                        "ANativeWindow_setBuffersGeometry(format=%d xr_surface=%d) => %d", format,
                        IsXrSurface() ? 1 : 0, geometry_result);

    if (egl_surface = eglCreateWindowSurface(egl_display, egl_config, host_window, 0);
        egl_surface == EGL_NO_SURFACE) {
        const EGLint egl_error = eglGetError();
        __android_log_print(ANDROID_LOG_ERROR, "CITRAVR_PORT",
                            "eglCreateWindowSurface failed: error=0x%x (%s)", egl_error,
                            EglErrorToString(egl_error));
        return {};
    }

    EGLint width = 0;
    EGLint height = 0;
    eglQuerySurface(egl_display, egl_surface, EGL_WIDTH, &width);
    eglQuerySurface(egl_display, egl_surface, EGL_HEIGHT, &height);
    __android_log_print(ANDROID_LOG_INFO, "CITRAVR_PORT", "egl surface created: size=%dx%d", width,
                        height);

    return egl_surface;
}

void EmuWindow_Android_OpenGL::DestroyWindowSurface() {
    if (!egl_surface) {
        return;
    }
    if (eglGetCurrentSurface(EGL_DRAW) == egl_surface) {
        eglMakeCurrent(egl_display, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
    }
    if (!eglDestroySurface(egl_display, egl_surface)) {
        LOG_CRITICAL(Frontend, "eglDestroySurface() failed");
    }
    egl_surface = EGL_NO_SURFACE;
}

void EmuWindow_Android_OpenGL::DestroyContext() {
    if (!egl_context) {
        return;
    }
    if (eglGetCurrentContext() == egl_context) {
        eglMakeCurrent(egl_display, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
    }
    if (!eglDestroyContext(egl_display, egl_context)) {
        LOG_CRITICAL(Frontend, "eglDestroySurface() failed");
    }
    if (!eglTerminate(egl_display)) {
        LOG_CRITICAL(Frontend, "eglTerminate() failed");
    }
    egl_context = EGL_NO_CONTEXT;
    egl_display = EGL_NO_DISPLAY;
}

std::unique_ptr<Frontend::GraphicsContext> EmuWindow_Android_OpenGL::CreateSharedContext() const {
    return std::make_unique<SharedContext_Android>(egl_display, egl_config, egl_context);
}

void EmuWindow_Android_OpenGL::PollEvents() {
    if (!render_window) {
        return;
    }

    host_window = render_window;
    render_window = nullptr;

    DestroyWindowSurface();
    CreateWindowSurface();
    OnFramebufferSizeChanged();
    presenting_state = PresentingState::Initial;
}

void EmuWindow_Android_OpenGL::StopPresenting() {
    if (presenting_state == PresentingState::Running) {
        eglMakeCurrent(egl_display, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
    }
    presenting_state = PresentingState::Stopped;
}

void EmuWindow_Android_OpenGL::PresentBootstrapFrame() {
    if (presenting_state != PresentingState::Running) [[unlikely]] {
        const EGLBoolean make_current =
            eglMakeCurrent(egl_display, egl_surface, egl_surface, egl_context);
        if (make_current != EGL_TRUE) {
            const EGLint egl_error = eglGetError();
            __android_log_print(ANDROID_LOG_ERROR, "CITRAVR_PORT",
                                "eglMakeCurrent failed during bootstrap: error=0x%x (%s)",
                                egl_error, EglErrorToString(egl_error));
            presenting_state = PresentingState::Stopped;
            return;
        }
        glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);
        presenting_state = PresentingState::Running;
    }
    if (presenting_state != PresentingState::Running) [[unlikely]] {
        return;
    }

    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);
    glViewport(0, 0, std::max(1, window_width), std::max(1, window_height));
    glClearColor(0.01f, 0.01f, 0.01f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    const EGLBoolean swap_result = eglSwapBuffers(egl_display, egl_surface);
    static uint64_t bootstrap_frame_counter = 0;
    ++bootstrap_frame_counter;
    if (swap_result != EGL_TRUE) {
        const EGLint egl_error = eglGetError();
        __android_log_print(ANDROID_LOG_ERROR, "CITRAVR_PORT",
                            "bootstrap eglSwapBuffers failed frame=%llu error=0x%x (%s)",
                            static_cast<unsigned long long>(bootstrap_frame_counter), egl_error,
                            EglErrorToString(egl_error));
        presenting_state = PresentingState::Stopped;
        return;
    }
    if (bootstrap_frame_counter == 1 || (bootstrap_frame_counter % 300) == 0) {
        __android_log_print(ANDROID_LOG_INFO, "CITRAVR_PORT",
                            "bootstrap eglSwapBuffers ok frame=%llu xr_surface=%d",
                            static_cast<unsigned long long>(bootstrap_frame_counter),
                            IsXrSurface() ? 1 : 0);
    }
}

void EmuWindow_Android_OpenGL::TryPresenting() {
    static uint64_t try_present_calls = 0;
    ++try_present_calls;
    if (try_present_calls == 1 || (try_present_calls % 600) == 0) {
        __android_log_print(ANDROID_LOG_INFO, "CITRAVR_PORT",
                            "TryPresenting entry calls=%llu state=%d xr_surface=%d",
                            static_cast<unsigned long long>(try_present_calls),
                            static_cast<int>(presenting_state), IsXrSurface() ? 1 : 0);
    }

    if (!system.IsPoweredOn()) {
        if (IsXrSurface()) {
            PresentBootstrapFrame();
            if (try_present_calls == 1 || (try_present_calls % 300) == 0) {
                __android_log_print(
                    ANDROID_LOG_INFO, "CITRAVR_PORT",
                    "TryPresenting bootstrap path: core not powered on, submitting placeholder");
            }
            return;
        }
        if ((try_present_calls % 300) == 0) {
            __android_log_print(ANDROID_LOG_INFO, "CITRAVR_PORT",
                                "TryPresenting skipped: system not powered on");
        }
        return;
    }
    if (presenting_state == PresentingState::Initial) [[unlikely]] {
        const EGLBoolean make_current =
            eglMakeCurrent(egl_display, egl_surface, egl_surface, egl_context);
        if (make_current != EGL_TRUE) {
            const EGLint egl_error = eglGetError();
            __android_log_print(ANDROID_LOG_ERROR, "CITRAVR_PORT",
                                "eglMakeCurrent failed during TryPresenting: error=0x%x (%s)",
                                egl_error, EglErrorToString(egl_error));
            presenting_state = PresentingState::Stopped;
            return;
        }
        glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);
        presenting_state = PresentingState::Running;
    }
    if (presenting_state != PresentingState::Running) [[unlikely]] {
        return;
    }
    eglSwapInterval(egl_display, Settings::values.use_vsync_new ? 1 : 0);
    system.GPU().Renderer().TryPresent(0);
    const EGLBoolean swap_result = eglSwapBuffers(egl_display, egl_surface);
    static uint64_t frame_counter = 0;
    ++frame_counter;
    if (swap_result != EGL_TRUE) {
        const EGLint egl_error = eglGetError();
        __android_log_print(ANDROID_LOG_ERROR, "CITRAVR_PORT",
                            "eglSwapBuffers failed frame=%llu error=0x%x (%s)",
                            static_cast<unsigned long long>(frame_counter), egl_error,
                            EglErrorToString(egl_error));
        presenting_state = PresentingState::Stopped;
        return;
    }
    if ((frame_counter % 300) == 0) {
        __android_log_print(ANDROID_LOG_INFO, "CITRAVR_PORT",
                            "eglSwapBuffers ok frame=%llu xr_surface=%d",
                            static_cast<unsigned long long>(frame_counter),
                            IsXrSurface() ? 1 : 0);
    }
}
