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

#pragma once

#include <stdint.h>

#include <memory>
//
//#include <EGL/egl.h>
//#include <EGL/eglext.h>

//#include <backend/DriverEnums.h>


namespace open3d {
namespace filament {

class PlatformEGLHeadless {
public:

    PlatformEGLHeadless() noexcept;
    ~PlatformEGLHeadless() noexcept;

    bool makeCurrent() noexcept;
    bool createDriver(void* sharedContext) noexcept;
    void terminate() noexcept;

    void* createSwapChain(void* nativewindow, uint64_t& flags) noexcept;
    void* createSwapChain(uint32_t width, uint32_t height, uint64_t& flags) noexcept;
    void destroySwapChain(void* swapChain) noexcept;
//    void makeCurrent(void* drawSwapChain, void* readSwapChain) noexcept;
//    void commit(void* swapChain) noexcept override;

//    bool canCreateFence() noexcept override { return true; }
//    Fence* createFence() noexcept override;
//    void destroyFence(Fence* fence) noexcept override;
//    backend::FenceStatus waitFence(Fence* fence, uint64_t timeout) noexcept override;
//
//    void createExternalImageTexture(void* texture) noexcept override;
//    void destroyExternalImage(void* texture) noexcept override;

    /* default no-op implementations... */

protected:
    static void logEglError(const char* name) noexcept;

    bool makeCurrent(void * drawSurface, void * readSurface) noexcept;
//    void initializeGlExtensions() noexcept;

    struct Impl;
    Impl* impl_;
};

} // namespace filament
} // namespace open3d

