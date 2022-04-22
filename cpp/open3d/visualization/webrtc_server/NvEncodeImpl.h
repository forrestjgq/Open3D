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
class IEncoder;
class OpenGLGraphicsDevice;
}
}
namespace open3d {
namespace visualization {
namespace webrtc_server {
class NvEncoderImpl : public unity::webrtc::IVideoEncoderObserver {
public:
    NvEncoderImpl();
    ~NvEncoderImpl();
    static NvEncoderImpl *GetInstance();
    void SetKeyFrame(uint32_t id);
    void SetRates(uint32_t id, uint32_t bitRate, int64_t frameRate);
    unity::webrtc::IEncoder* AddEncoder(int width, int height);
    void SaveEncoder(unity::webrtc::IEncoder*);
    void RemoveEncoder(uint32_t id);
    void EncodeFrame(uint32_t id, void *frame);
    void DelegateOnFrame(const ::webrtc::VideoFrame& frame);
private:
    std::map<const uint32_t, std::unique_ptr<unity::webrtc::IEncoder>> m_mapIdAndEncoder;
    std::unique_ptr<unity::webrtc::OpenGLGraphicsDevice> m_device;
    std::unique_ptr <webrtc::Clock> m_clock;
    static uint32_t s_encoderId;
    static uint32_t GenerateUniqueId();
};
}
}
}
#endif  // OPEN3D_NVENCODEIMPL_H
