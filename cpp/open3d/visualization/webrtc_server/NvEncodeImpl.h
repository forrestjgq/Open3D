//
// Created by forrest on 2022/4/19.
//

#ifndef OPEN3D_NVENCODEIMPL_H
#define OPEN3D_NVENCODEIMPL_H

#pragma once
#include <map>
#include <memory>
#include <mutex>


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
    namespace gui {
        class Window;
    }
namespace webrtc_server {
class NvEncoderImpl : public unity::webrtc::IVideoEncoderObserver {
public:
    NvEncoderImpl();
    ~NvEncoderImpl();
    static NvEncoderImpl *GetInstance();
    void SetKeyFrame(uint32_t id);
    void SetRates(uint32_t id, uint32_t bitRate, int64_t frameRate);
    void AddEncoder(int width, int height, gui::Window *o3d, std::function<void (unity::webrtc::NvEncoder *)> callback);
    void AddEncoderEx(int width, int height, std::function<void (unity::webrtc::NvEncoder *)> callback);
    void RemoveEncoder(uint32_t id);
    void EncodeFrame(uint32_t id, const std::shared_ptr<core::Tensor>& frame);

#if CTX_TYPE == CTX_FILAMENT
    void Activate(gui::Window *o3d);
    void FilamentCallback(const char *action, void *a1, void *a2, void *a3);
#endif
private:
    std::map<const uint32_t, std::shared_ptr<unity::webrtc::NvEncoder>> m_mapIdAndEncoder;
    std::unique_ptr<unity::webrtc::OpenGLGraphicsDevice> m_device;
    std::unique_ptr <webrtc::Clock> m_clock;
    static uint32_t s_encoderId;
    static uint32_t GenerateUniqueId();


#if CTX_TYPE == CTX_FILAMENT
    bool m_activated = false;
    std::mutex mt_;
    std::vector<std::function<void()>> q_;
#endif
};
}
}
}
#endif  // OPEN3D_NVENCODEIMPL_H
