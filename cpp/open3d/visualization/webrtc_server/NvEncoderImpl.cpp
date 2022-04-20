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

static UnityRenderingExtTextureFormat default_texture = kUnityRenderingExtFormatR8G8B8_SRGB;
//#pragma region open an encode session
uint32_t NvEncoderImpl::s_encoderId = 0;
uint32_t NvEncoderImpl::GenerateUniqueId() { return s_encoderId++; }
//#pragma endregion

NvEncoderImpl::NvEncoderImpl() {
    m_device = std::make_unique<unity::webrtc::OpenGLGraphicsDevice>();
    m_device->InitV();
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

void NvEncoderImpl::AddEncoder(int width, int height) {
    auto dev = (unity::webrtc::IGraphicsDevice *)m_device.get();
    auto encoder = std::make_unique<unity::webrtc::NvEncoderGL>(width, height, dev, default_texture);
    encoder->InitV();
    auto id = GenerateUniqueId();
    m_mapIdAndEncoder[id] = std::move(encoder);
    encoder->SetEncoderId(id);
}
void NvEncoderImpl::RemoveEncoder(uint32_t id) {
    m_mapIdAndEncoder.erase(id);
}
}
}
}
