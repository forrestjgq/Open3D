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
    void SetKeyFrame(uint32_t id);
    void SetRates(uint32_t id, uint32_t bitRate, int64_t frameRate);
    void AddEncoder(int width, int height);
    void RemoveEncoder(uint32_t id);

private:
    std::map<const uint32_t, std::unique_ptr<unity::webrtc::IEncoder>> m_mapIdAndEncoder;
    std::unique_ptr<unity::webrtc::OpenGLGraphicsDevice> m_device;
    static uint32_t s_encoderId;
    static uint32_t GenerateUniqueId();
};
}
}
}
#endif  // OPEN3D_NVENCODEIMPL_H
