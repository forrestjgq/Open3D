//
// Created by forrest on 2022/4/19.
//
#include "open3d/visualization/webrtc_server/nvenc/pch.h"
#include "open3d/visualization/webrtc_server/nvenc/DummyVideoEncoder.h"
#include "open3d/visualization/webrtc_server/NvEncodeImpl.h"
#include "open3d/visualization/webrtc_server/nvenc/NvCodec/codec/NvEncoderGL.h"
#include "open3d/visualization/webrtc_server/nvenc/GraphicsDevice/OpenGL/OpenGLGraphicsDevice.h"
#include "open3d/visualization/gui/Window.h"
#include <memory>

namespace open3d {
namespace visualization {
namespace webrtc_server {

#if CTX_TYPE == CTX_FILAMENT
static void _FilamentCallback(const char *action, void *user, void *a1, void *a2, void *a3) {
    ((NvEncoderImpl*)user)->FilamentCallback(action, a1, a2, a3);
}
#endif
NvEncoderImpl *NvEncoderImpl::GetInstance() {
    static NvEncoderImpl impl;
    return &impl;
}
static UnityRenderingExtTextureFormat default_texture = kUnityRenderingExtFormatR8G8B8A8_SRGB;
//#pragma region open an encode session
uint32_t NvEncoderImpl::s_encoderId = 1;
uint32_t NvEncoderImpl::GenerateUniqueId() { return s_encoderId++; }
//#pragma endregion

NvEncoderImpl::NvEncoderImpl() {
    m_device = std::make_unique<unity::webrtc::OpenGLGraphicsDevice>();
    m_device->InitV();
    m_clock.reset(webrtc::Clock::GetRealTimeClock());
}
NvEncoderImpl::~NvEncoderImpl() {
    m_device->ShutdownV();
}
#if CTX_TYPE == CTX_FILAMENT
void NvEncoderImpl::Activate(gui::Window *o3d) {
    if(!m_activated) {
        m_activated = true;
        o3d->SetRenderCallback(_FilamentCallback, this);
    }
}
void NvEncoderImpl::FilamentCallback(const char *action, void *a1, void *a2, void *a3) {
    std::string a(action);
    if (a != "readPixels" && a != "commit") {
        return;
    }
    decltype(q_) v;
    std::vector<std::shared_ptr<unity::webrtc::NvEncoder>> encoders;
    {
        std::unique_lock<std::mutex> lock(mt_);
        v = std::move(q_);
        for (auto it = m_mapIdAndEncoder.begin(); it != m_mapIdAndEncoder.end();++it) {
            encoders.push_back(it->second);
        }
    }
    for (auto &d : v) {
        d();
    }
    for (auto &e: encoders) {
        if(a == "readPixels") {
            e->RecvFromFilament(a1, a2);
        } else {
            e->WaitFrameDone();
        }
    }
}
#endif
void NvEncoderImpl::SetKeyFrame(uint32_t id) {
    if (m_mapIdAndEncoder.count(id)) {
        m_mapIdAndEncoder[id]->SetIdrFrame();
    }
}

void NvEncoderImpl::SetRates(uint32_t id, uint32_t bitRate, int64_t frameRate) {
    if(m_mapIdAndEncoder.count(id)) {
        m_mapIdAndEncoder[id]->SetRates(bitRate, frameRate);
    }
}

void NvEncoderImpl::AddEncoder(int width, int height, gui::Window* o3d, std::function<void (unity::webrtc::NvEncoder *)> callback) {
#if CTX_TYPE == CTX_FILAMENT
    Activate(o3d);
    std::unique_lock<std::mutex> lock(mt_);
    q_.push_back([=](){this->AddEncoderEx(width, height, callback);});
#else
    AddEncoderEx(width, height, std::move(callback));
#endif
}
void NvEncoderImpl::AddEncoderEx(int width, int height, std::function<void (unity::webrtc::NvEncoder *)> callback) {
    open3d::utility::LogInfo("create encoder");
    auto dev = (unity::webrtc::IGraphicsDevice *)m_device.get();
    auto encoder = std::make_shared<unity::webrtc::NvEncoderGL>(width, height, dev, default_texture);
    encoder->InitInternal();
    auto id = GenerateUniqueId();
    encoder->SetEncoderId(id);
    m_mapIdAndEncoder[encoder->Id()] = encoder;
    callback(encoder.get());
}
void NvEncoderImpl::RemoveEncoder(uint32_t id) {
    m_mapIdAndEncoder.erase(id);
}
void NvEncoderImpl::EncodeFrame(uint32_t id, const std::shared_ptr<core::Tensor>& frame) {
//#if defined(CUDA_PLATFORM)
//    (void) m_device->GetCUcontext();
//#else
//#error set opengl context
//#endif
#if CTX_TYPE != CTX_FILAMENT
    if(m_mapIdAndEncoder.count(id)) {
        m_mapIdAndEncoder[id]->RecvOpen3DCPUFrame(frame);
    }
#endif
}
}
}
}
