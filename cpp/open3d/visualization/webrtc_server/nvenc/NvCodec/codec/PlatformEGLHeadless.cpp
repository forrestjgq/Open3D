/*
 * Copyright (C) 2017 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

//#define FILAMENT_USE_EXTERNAL_GLES3 1

#include <iostream>
#include "open3d/visualization/webrtc_server/nvenc/NvCodec/codec/PlatformEGLHeadless.h"
#include <unordered_set>
#include <cstring>

#include <EGL/egl.h>
#include <EGL/eglext.h>


#include <assert.h>
#include <dlfcn.h>

#define LIBRARY_EGL "libEGL.so.1"

namespace open3d {
namespace filament {

#define UTILS_PRIVATE
#   define UTILS_LIKELY( exp )    (!!(exp))
#   define UTILS_UNLIKELY( exp )  (!!(exp))

// The Android NDK doesn't exposes extensions, fake it with eglGetProcAddress
namespace glext {
UTILS_PRIVATE PFNEGLCREATESYNCKHRPROC eglCreateSyncKHR = {};
UTILS_PRIVATE PFNEGLDESTROYSYNCKHRPROC eglDestroySyncKHR = {};
UTILS_PRIVATE PFNEGLCLIENTWAITSYNCKHRPROC eglClientWaitSyncKHR = {};
UTILS_PRIVATE PFNEGLCREATEIMAGEKHRPROC eglCreateImageKHR = {};
UTILS_PRIVATE PFNEGLDESTROYIMAGEKHRPROC eglDestroyImageKHR = {};
}
using namespace glext;

// Stores EGL function pointers and a handle to the system's EGL library
struct EGLFunctions 
{
    PFNEGLGETDISPLAYPROC getDisplay;
    PFNEGLINITIALIZEPROC initialize;
    PFNEGLQUERYSTRINGPROC queryString;
    PFNEGLCHOOSECONFIGPROC chooseConfig;
    PFNEGLCREATEPBUFFERSURFACEPROC createPbufferSurface;
    PFNEGLBINDAPIPROC bindAPI;
    PFNEGLCREATECONTEXTPROC createContext;
    PFNEGLMAKECURRENTPROC makeCurrent;
    PFNEGLSWAPBUFFERSPROC swapBuffers;
    PFNEGLDESTROYSURFACEPROC destroySurface;
    PFNEGLDESTROYCONTEXTPROC destroyContext;
    PFNEGLTERMINATEPROC terminate;
    PFNEGLRELEASETHREADPROC releaseThread;
    PFNEGLCREATEWINDOWSURFACEPROC createWindowSurface;
    PFNEGLSURFACEATTRIBPROC surfaceAttrib;
    PFNEGLGETERRORPROC getError;
    
    void* library;
} g_egl;
    
// ---------------------------------------------------------------------------------------------
// Utilities
// ---------------------------------------------------------------------------------------------

void PlatformEGLHeadless::logEglError(const char* name) noexcept {
    const char* err;
    switch (g_egl.getError()) {
        case EGL_NOT_INITIALIZED:       err = "EGL_NOT_INITIALIZED";    break;
        case EGL_BAD_ACCESS:            err = "EGL_BAD_ACCESS";         break;
        case EGL_BAD_ALLOC:             err = "EGL_BAD_ALLOC";          break;
        case EGL_BAD_ATTRIBUTE:         err = "EGL_BAD_ATTRIBUTE";      break;
        case EGL_BAD_CONTEXT:           err = "EGL_BAD_CONTEXT";        break;
        case EGL_BAD_CONFIG:            err = "EGL_BAD_CONFIG";         break;
        case EGL_BAD_CURRENT_SURFACE:   err = "EGL_BAD_CURRENT_SURFACE";break;
        case EGL_BAD_DISPLAY:           err = "EGL_BAD_DISPLAY";        break;
        case EGL_BAD_SURFACE:           err = "EGL_BAD_SURFACE";        break;
        case EGL_BAD_MATCH:             err = "EGL_BAD_MATCH";          break;
        case EGL_BAD_PARAMETER:         err = "EGL_BAD_PARAMETER";      break;
        case EGL_BAD_NATIVE_PIXMAP:     err = "EGL_BAD_NATIVE_PIXMAP";  break;
        case EGL_BAD_NATIVE_WINDOW:     err = "EGL_BAD_NATIVE_WINDOW";  break;
        case EGL_CONTEXT_LOST:          err = "EGL_CONTEXT_LOST";       break;
        default:                        err = "unknown";                break;
    }
    std::cout << name << " failed with " << err << std::endl;
}
class unordered_string_set : public std::unordered_set<std::string> {
public:
    bool has(const char* str) {
        return find(std::string(str)) != end();
    }
};

inline unordered_string_set split(const char* spacedList) {
    unordered_string_set set;
    const char* current = spacedList;
    const char* head = current;
    do {
        head = strchr(current, ' ');
        std::string s(current, head ? head - current : strlen(current));
        if (s.length()) {
            set.insert(std::move(s));
        }
        current = head + 1;
    } while (head);
    return set;
}

// ---------------------------------------------------------------------------------------------

struct PlatformEGLHeadless::Impl {
    EGLDisplay mEGLDisplay = EGL_NO_DISPLAY;
    EGLContext mEGLContext = EGL_NO_CONTEXT;
    EGLSurface mCurrentDrawSurface = EGL_NO_SURFACE;
    EGLSurface mCurrentReadSurface = EGL_NO_SURFACE;
    EGLSurface mEGLDummySurface = EGL_NO_SURFACE;
    EGLConfig mEGLConfig = EGL_NO_CONFIG_KHR;
};
PlatformEGLHeadless::PlatformEGLHeadless() noexcept : impl_(new PlatformEGLHeadless::Impl()){}
PlatformEGLHeadless::~PlatformEGLHeadless() noexcept { delete impl_;}

static PFNEGLGETPROCADDRESSPROC getProcAddress;

static bool loadLibraries()
{
    g_egl.library = dlopen(LIBRARY_EGL, RTLD_LOCAL | RTLD_NOW);
    if(!g_egl.library) {
        std::cout << "Could not find library " << LIBRARY_EGL << std::endl;
        return false;
    }
    
    getProcAddress = (PFNEGLGETPROCADDRESSPROC)dlsym(g_egl.library, "eglGetProcAddress");
    g_egl.getDisplay = (PFNEGLGETDISPLAYPROC) getProcAddress("eglGetDisplay");
    g_egl.initialize = (PFNEGLINITIALIZEPROC) getProcAddress("eglInitialize");
    g_egl.queryString = (PFNEGLQUERYSTRINGPROC) getProcAddress("eglQueryString");
    g_egl.chooseConfig = (PFNEGLCHOOSECONFIGPROC) getProcAddress("eglChooseConfig");
    g_egl.createPbufferSurface = (PFNEGLCREATEPBUFFERSURFACEPROC) getProcAddress("eglCreatePbufferSurface");
    g_egl.bindAPI = (PFNEGLBINDAPIPROC) getProcAddress("eglBindAPI");
    g_egl.createContext = (PFNEGLCREATECONTEXTPROC) getProcAddress("eglCreateContext");
    g_egl.makeCurrent = (PFNEGLMAKECURRENTPROC) getProcAddress("eglMakeCurrent");
    g_egl.swapBuffers = (PFNEGLSWAPBUFFERSPROC) getProcAddress("eglSwapBuffers");
    g_egl.destroySurface = (PFNEGLDESTROYSURFACEPROC) getProcAddress("eglDestroySurface");
    g_egl.destroyContext = (PFNEGLDESTROYCONTEXTPROC) getProcAddress("eglDestroyContext");
    g_egl.terminate = (PFNEGLTERMINATEPROC) getProcAddress("eglTerminate");
    g_egl.releaseThread = (PFNEGLRELEASETHREADPROC) getProcAddress("eglReleaseThread");
    g_egl.createWindowSurface = (PFNEGLCREATEWINDOWSURFACEPROC) getProcAddress("eglCreateWindowSurface");
    g_egl.surfaceAttrib = (PFNEGLSURFACEATTRIBPROC) getProcAddress("eglSurfaceAttrib");
    g_egl.getError = (PFNEGLGETERRORPROC) getProcAddress("eglGetError");
    
    return true;
}

bool PlatformEGLHeadless::createDriver(void* sharedContext) noexcept {
    if (UTILS_UNLIKELY(!loadLibraries())) {
        return false;
    }
    
    impl_->mEGLDisplay = g_egl.getDisplay(EGL_DEFAULT_DISPLAY);
    assert(impl_->mEGLDisplay != EGL_NO_DISPLAY);

//    int retval;
    EGLint major, minor;
    EGLBoolean initialized = g_egl.initialize(impl_->mEGLDisplay, &major, &minor);
    if (UTILS_UNLIKELY(!initialized)) {
        std::cout << "eglInitialize failed" << std::endl;
        return false;
    }

    std::cout << "EGL(" << major << "." << minor << ")" << std::endl;
    
    auto extensions = split(g_egl.queryString(impl_->mEGLDisplay, EGL_EXTENSIONS));

    eglCreateSyncKHR = (PFNEGLCREATESYNCKHRPROC) getProcAddress("eglCreateSyncKHR");
    eglDestroySyncKHR = (PFNEGLDESTROYSYNCKHRPROC) getProcAddress("eglDestroySyncKHR");
    eglClientWaitSyncKHR = (PFNEGLCLIENTWAITSYNCKHRPROC) getProcAddress("eglClientWaitSyncKHR");

    eglCreateImageKHR = (PFNEGLCREATEIMAGEKHRPROC) getProcAddress("eglCreateImageKHR");
    eglDestroyImageKHR = (PFNEGLDESTROYIMAGEKHRPROC) getProcAddress("eglDestroyImageKHR");

    // Config suitable for OpenGL and offscreen rendering
    EGLint configsCount;
    EGLint configAttribs[] = {
            EGL_RENDERABLE_TYPE, EGL_OPENGL_BIT,
            EGL_SURFACE_TYPE, EGL_PBUFFER_BIT,
            EGL_RED_SIZE,    8,
            EGL_GREEN_SIZE,  8,
            EGL_BLUE_SIZE,   8,
            EGL_ALPHA_SIZE,  8,
            EGL_DEPTH_SIZE, 24,
            EGL_NONE
    };

    EGLint contextAttribs[] = {
        // Filament requires OpenGL 4.1+
        EGL_CONTEXT_MAJOR_VERSION, 4,
        EGL_CONTEXT_MINOR_VERSION, 1,
        EGL_NONE, EGL_NONE,
        EGL_NONE
    };

    EGLint pbufferAttribs[] = {
            EGL_WIDTH,  1,
            EGL_HEIGHT, 1,
            EGL_NONE
    };

#ifdef NDEBUG
    // When we don't have a shared context and we're in release mode, we always activate the
    // EGL_KHR_create_context_no_error extension.
    if (!sharedContext && extensions.has("EGL_KHR_create_context_no_error")) {
        contextAttribs[4] = EGL_CONTEXT_OPENGL_NO_ERROR_KHR;
        contextAttribs[5] = EGL_TRUE;
    }
#endif

    // Get config
    if (!g_egl.chooseConfig(impl_->mEGLDisplay, configAttribs, &impl_->mEGLConfig, 1, &configsCount)) {
        logEglError("eglChooseConfig");
        goto error;
    }

    if (configsCount == 0) {
        std::cout << "Failed to find any suitable EGL configs" << std::endl;
        goto error;
    }

    impl_->mEGLDummySurface = g_egl.createPbufferSurface(impl_->mEGLDisplay, impl_->mEGLConfig, pbufferAttribs);
    if (impl_->mEGLDummySurface == EGL_NO_SURFACE) {
        logEglError("eglCreatePbufferSurface");
        goto error;
    }

    // EGL Headless uses OpenGL API
    if(!g_egl.bindAPI(EGL_OPENGL_API)) {
        logEglError("eglBindAPI");
    }
    
    impl_->mEGLContext = g_egl.createContext(impl_->mEGLDisplay, impl_->mEGLConfig, (EGLContext)sharedContext, contextAttribs);
    if (UTILS_UNLIKELY(impl_->mEGLContext == EGL_NO_CONTEXT)) {
        // eglCreateContext failed
        logEglError("eglCreateContext");
        goto error;
    }

    if (!makeCurrent(impl_->mEGLDummySurface, impl_->mEGLDummySurface)) {
        // eglMakeCurrent failed
        logEglError("eglMakeCurrent");
        goto error;
    }
    return true;


error:
    // if we're here, we've failed
    if (impl_->mEGLDummySurface) {
        g_egl.destroySurface(impl_->mEGLDisplay, impl_->mEGLDummySurface);
    }
    if (impl_->mEGLContext) {
        g_egl.destroyContext(impl_->mEGLDisplay, impl_->mEGLContext);
    }

    impl_->mEGLDummySurface = EGL_NO_SURFACE;
    impl_->mEGLContext = EGL_NO_CONTEXT;

    g_egl.terminate(impl_->mEGLDisplay);
    g_egl.releaseThread();

    return false;
}

bool PlatformEGLHeadless::makeCurrent() noexcept {
    return g_egl.makeCurrent(impl_->mEGLDisplay, impl_->mCurrentDrawSurface, impl_->mCurrentReadSurface, impl_->mEGLContext);
}
bool PlatformEGLHeadless::makeCurrent(void* drawSurface, void* readSurface) noexcept {
    if (UTILS_UNLIKELY((drawSurface != impl_->mCurrentDrawSurface || readSurface != impl_->mCurrentReadSurface))) {
        impl_->mCurrentDrawSurface = drawSurface;
        impl_->mCurrentReadSurface = readSurface;
        return g_egl.makeCurrent(impl_->mEGLDisplay, drawSurface, readSurface, impl_->mEGLContext);
    }
    return true;
}

void PlatformEGLHeadless::terminate() noexcept {
    g_egl.makeCurrent(impl_->mEGLDisplay, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
    g_egl.destroySurface(impl_->mEGLDisplay, impl_->mEGLDummySurface);
    g_egl.destroyContext(impl_->mEGLDisplay, impl_->mEGLContext);
    g_egl.terminate(impl_->mEGLDisplay);
    g_egl.releaseThread();
}

void * PlatformEGLHeadless::createSwapChain(
        void* nativeWindow, uint64_t& flags) noexcept {
    // NOTE: ignoring backend::SWAP_CHAIN_CONFIG_TRANSPARENT flag and always
    // returning a surface based on same config
    EGLSurface sur = g_egl.createWindowSurface(impl_->mEGLDisplay, impl_->mEGLConfig,
            (EGLNativeWindowType)nativeWindow, nullptr);

    if (UTILS_UNLIKELY(sur == EGL_NO_SURFACE)) {
        logEglError("eglCreateWindowSurface");
        return nullptr;
    }
    if (!g_egl.surfaceAttrib(impl_->mEGLDisplay, sur, EGL_SWAP_BEHAVIOR, EGL_BUFFER_DESTROYED)) {
        logEglError("eglSurfaceAttrib(..., EGL_SWAP_BEHAVIOR, EGL_BUFFER_DESTROYED)");
        // this is not fatal
    }
    return (void*)sur;
}

void* PlatformEGLHeadless::createSwapChain(
        uint32_t width, uint32_t height, uint64_t& flags) noexcept {

    EGLint attribs[] = {
            EGL_WIDTH, EGLint(width),
            EGL_HEIGHT, EGLint(height),
            EGL_NONE
    };

    // NOTE: see note above regarding flags
    EGLSurface sur = g_egl.createPbufferSurface(impl_->mEGLDisplay, impl_->mEGLConfig, attribs);

    if (UTILS_UNLIKELY(sur == EGL_NO_SURFACE)) {
        logEglError("eglCreatePbufferSurface");
        return nullptr;
    }
    return (void*)sur;
}

void PlatformEGLHeadless::destroySwapChain(void* swapChain) noexcept {
    EGLSurface sur = (EGLSurface) swapChain;
    if (sur != EGL_NO_SURFACE) {
        makeCurrent(impl_->mEGLDummySurface, impl_->mEGLDummySurface);
        g_egl.destroySurface(impl_->mEGLDisplay, sur);
    }
}

//void PlatformEGLHeadless::makeCurrent(Platform::SwapChain* drawSwapChain,
//                              Platform::SwapChain* readSwapChain) noexcept {
//    EGLSurface drawSur = (EGLSurface) drawSwapChain;
//    EGLSurface readSur = (EGLSurface) readSwapChain;
//    if (drawSur != EGL_NO_SURFACE || readSur != EGL_NO_SURFACE) {
//        makeCurrent(drawSur, readSur);
//    }
//}
//
//void PlatformEGLHeadless::commit(Platform::SwapChain* swapChain) noexcept {
//    EGLSurface sur = (EGLSurface) swapChain;
//    if (sur != EGL_NO_SURFACE) {
//        g_egl.swapBuffers(mEGLDisplay, sur);
//    }
//}
//
//Platform::Fence* PlatformEGLHeadless::createFence() noexcept {
//    Fence* f = nullptr;
//#ifdef EGL_KHR_reusable_sync
//    f = (Fence*) eglCreateSyncKHR(mEGLDisplay, EGL_SYNC_FENCE_KHR, nullptr);
//#endif
//    return f;
//}
//
//void PlatformEGLHeadless::destroyFence(Platform::Fence* fence) noexcept {
//#ifdef EGL_KHR_reusable_sync
//    EGLSyncKHR sync = (EGLSyncKHR) fence;
//    if (sync != EGL_NO_SYNC_KHR) {
//        eglDestroySyncKHR(mEGLDisplay, sync);
//    }
//#endif
//}
//
//backend::FenceStatus PlatformEGLHeadless::waitFence(
//        Platform::Fence* fence, uint64_t timeout) noexcept {
//#ifdef EGL_KHR_reusable_sync
//    EGLSyncKHR sync = (EGLSyncKHR) fence;
//    if (sync != EGL_NO_SYNC_KHR) {
//        EGLint status = eglClientWaitSyncKHR(mEGLDisplay, sync, 0, (EGLTimeKHR)timeout);
//        if (status == EGL_CONDITION_SATISFIED_KHR) {
//            return FenceStatus::CONDITION_SATISFIED;
//        }
//        if (status == EGL_TIMEOUT_EXPIRED_KHR) {
//            return FenceStatus::TIMEOUT_EXPIRED;
//        }
//    }
//#endif
//    return FenceStatus::ERROR;
//}
//
//void PlatformEGLHeadless::createExternalImageTexture(void* texture) noexcept {
//    auto* t = (OpenGLDriver::GLTexture*) texture;
//    glGenTextures(1, &t->gl.id);
//    t->gl.target = GL_TEXTURE_2D;
//    t->gl.targetIndex = (uint8_t)
//                OpenGLContext::getIndexForTextureTarget(GL_TEXTURE_2D);
//}
//
//void PlatformEGLHeadless::destroyExternalImage(void* texture) noexcept {
//    auto* t = (OpenGLDriver::GLTexture*) texture;
//    glDeleteTextures(1, &t->gl.id);
//}
//
//void PlatformEGLHeadless::initializeGlExtensions() noexcept {
//    GLint major = 0;
//    GLint minor = 0;
//    glGetIntegerv(GL_MAJOR_VERSION, &major);
//    glGetIntegerv(GL_MINOR_VERSION, &minor);
//    slog.i << "OpenGL(" << major << "." << minor << ")" << io::endl;
//
//    // NOTE: I'm not sure what this does. Maybe just a sanity check for
//    // debugging purposes?
//    GLUtils::unordered_string_set glExtensions;
//    GLint n;
//    glGetIntegerv(GL_NUM_EXTENSIONS, &n);
//    for (GLint i = 0; i < n; ++i) {
//        const char * const extension = (const char*) glGetStringi(GL_EXTENSIONS, (GLuint)i);
//        glExtensions.insert(extension);
//    }
//}

} // namespace filament
} // namespace open3d
// ---------------------------------------------------------------------------------------------
