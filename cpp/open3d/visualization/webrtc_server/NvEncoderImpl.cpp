//
// Created by forrest on 2022/4/19.
//
#include "open3d/visualization/webrtc_server/nvenc/pch.h"
#include "open3d/visualization/webrtc_server/nvenc/DummyVideoEncoder.h"
#include "open3d/visualization/webrtc_server/NvEncodeImpl.h"
#include "open3d/visualization/webrtc_server/nvenc/NvCodec/codec/NvEncoderGL.h"
#include "open3d/visualization/webrtc_server/nvenc/GraphicsDevice/OpenGL/OpenGLGraphicsDevice.h"
#include <memory>

namespace open3d {
namespace visualization {
namespace webrtc_server {

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

std::shared_ptr<unity::webrtc::NvEncoder> NvEncoderImpl::AddEncoder(int width, int height) {
//#if defined(CUDA_PLATFORM)
//    (void) m_device->GetCUcontext();
//#else
//#error set opengl context
//#endif
    auto dev = (unity::webrtc::IGraphicsDevice *)m_device.get();
    auto encoder = std::make_shared<unity::webrtc::NvEncoderGL>(width, height, dev, default_texture);
    encoder->InitInternal();
    auto id = GenerateUniqueId();
    encoder->SetEncoderId(id);
    m_mapIdAndEncoder[encoder->Id()] = encoder;
    return encoder;
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

    if(m_mapIdAndEncoder.count(id)) {
        m_mapIdAndEncoder[id]->RecvOpen3DCPUFrame(frame);
    }
}
}
}
}
