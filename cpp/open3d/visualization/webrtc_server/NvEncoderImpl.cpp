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
static UnityRenderingExtTextureFormat default_texture = kUnityRenderingExtFormatR8G8B8_SRGB;
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

void NvEncoderImpl::DelegateOnFrame(const ::webrtc::VideoFrame& frame) {}
unity::webrtc::IEncoder* NvEncoderImpl::AddEncoder(int width, int height) {
#if defined(CUDA_PLATFORM)
    (void) m_device->GetCUcontext();
#else
#error set opengl context
#endif
    auto dev = (unity::webrtc::IGraphicsDevice *)m_device.get();
    auto encoder = new unity::webrtc::NvEncoderGL(width, height, dev, default_texture);
    encoder->InitV();
    auto id = GenerateUniqueId();
    encoder->SetEncoderId(id);
    return encoder;
}
void NvEncoderImpl::SaveEncoder(unity::webrtc::IEncoder *encoder) {
    auto t = std::unique_ptr<unity::webrtc::IEncoder>(encoder);
    m_mapIdAndEncoder[encoder->Id()] = std::move(t);
}
void NvEncoderImpl::RemoveEncoder(uint32_t id) {
    m_mapIdAndEncoder.erase(id);
}
void NvEncoderImpl::EncodeFrame(uint32_t id, void *frame) {
#if defined(CUDA_PLATFORM)
    (void) m_device->GetCUcontext();
#else
#error set opengl context
#endif

    if(m_mapIdAndEncoder.count(id)) {
        m_mapIdAndEncoder[id]->MakeContextCurrent();
        m_mapIdAndEncoder[id]->CopyBufferFromCPU(frame);
        int64_t timestamp_us = m_clock->TimeInMicroseconds();
        m_mapIdAndEncoder[id]->EncodeFrame(timestamp_us);
    }
}
}
}
}
