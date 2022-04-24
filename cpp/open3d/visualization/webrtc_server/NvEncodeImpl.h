//
// Created by forrest on 2022/4/19.
//

#ifndef OPEN3D_NVENCODEIMPL_H
#define OPEN3D_NVENCODEIMPL_H

#pragma once
#include <map>
#include <memory>


namespace unity {
namespace webrtc {
class OpenGLGraphicsDevice;
class NvEncoder;
}
}
namespace open3d {
namespace core {
    class Tensor;
}
namespace visualization {
namespace webrtc_server {
class NvEncoderImpl : public unity::webrtc::IVideoEncoderObserver {
public:
    NvEncoderImpl();
    ~NvEncoderImpl();
    static NvEncoderImpl *GetInstance();
    void SetKeyFrame(uint32_t id);
    void SetRates(uint32_t id, uint32_t bitRate, int64_t frameRate);
    std::shared_ptr<unity::webrtc::NvEncoder> AddEncoder(int width, int height);
    void RemoveEncoder(uint32_t id);
    void EncodeFrame(uint32_t id, const std::shared_ptr<core::Tensor>& frame);
private:
    std::map<const uint32_t, std::shared_ptr<unity::webrtc::NvEncoder>> m_mapIdAndEncoder;
    std::unique_ptr<unity::webrtc::OpenGLGraphicsDevice> m_device;
    std::unique_ptr <webrtc::Clock> m_clock;
    static uint32_t s_encoderId;
    static uint32_t GenerateUniqueId();
};
}
}
}
#endif  // OPEN3D_NVENCODEIMPL_H
