#pragma once
#include <vector>
#include <rtc_base/timestamp_aligner.h>
#include <mutex>
#include <thread>

#include "open3d/visualization/webrtc_server/nvenc/NvCodec/include/nvEncodeAPI.h"
#include "open3d/visualization/webrtc_server/nvenc/IEncoder.h"
#include "open3d/core/Tensor.h"
namespace open3d {
namespace filament {
class PlatformEGLHeadless;
}
}
namespace unity
{
namespace webrtc
{
    using OutputFrame = NV_ENC_OUTPUT_PTR;
    class ITexture2D;
    class IGraphicsDevice;
    class Scheduler;
    class GLContext;
    class NvEncoder : public IEncoder
    {
    private:
        struct InputFrame
        {
            NV_ENC_REGISTERED_PTR registeredResource;
            NV_ENC_INPUT_PTR mappedResource;
            NV_ENC_BUFFER_FORMAT bufferFormat;
        };

        struct Frame
        {
            InputFrame inputFrame = {nullptr, nullptr, NV_ENC_BUFFER_FORMAT_UNDEFINED };
            OutputFrame outputFrame = nullptr;
            std::vector<uint8> encodedFrame = {};
        };
    public:
        NvEncoder(
            NV_ENC_DEVICE_TYPE type,
            NV_ENC_INPUT_RESOURCE_TYPE inputType,
            NV_ENC_BUFFER_FORMAT bufferFormat,
            int width,
            int height,
            IGraphicsDevice* device,
            UnityRenderingExtTextureFormat textureFormat);
        ~NvEncoder() override;

        void doInContext(std::function<void()> &&f);
        void RecvOpen3DCPUFrame(const std::shared_ptr<open3d::core::Tensor>& frame);
        void RecvFromFilament(void *fbo, void *pbo);
        void WaitFrameDone();
        void SetRates(uint32_t bitRate, int64_t frameRate) override;
        bool CopyBuffer(void* frame) override;
        bool CopyBufferFromCPU(void* frame) override;
        bool EncodeFrame(int64_t timestamp_us) override;
        int32 EncodeFrameEx(int64_t timestamp_us);
        bool IsSupported() const override { return m_isNvEncoderSupported; }
        void SetIdrFrame()  override;
        uint64 GetCurrentFrameCount() const override { return frameCount; }
        void InitV() override;
        void InitInternal();
    protected:
        void Release();
        bool Keep();
        void UpdateSettings() override;
        static CodecInitializationResult LoadCodec();
        static bool LoadModule();
        static bool CheckDriverVersion();
        static void UnloadModule();
        static uint32_t GetNumChromaPlanes(NV_ENC_BUFFER_FORMAT);
        static uint32_t GetChromaHeight(const NV_ENC_BUFFER_FORMAT bufferFormat, const uint32_t lumaHeight);
        static uint32_t GetWidthInBytes(const NV_ENC_BUFFER_FORMAT bufferFormat, const uint32_t width);
        void WorkInPassive();

    protected:

        int m_width;
        int m_height;
        IGraphicsDevice* m_device;
        UnityRenderingExtTextureFormat m_textureFormat;

        NV_ENC_DEVICE_TYPE m_deviceType;
        NV_ENC_INPUT_RESOURCE_TYPE m_inputType;
        NV_ENC_BUFFER_FORMAT m_bufferFormat;

        bool m_isNvEncoderSupported = false;

        virtual std::shared_ptr<void> AllocateInputResourceV(ITexture2D* tex) = 0;

        void InitEncoderResources();
        void ReleaseEncoderResources();

    private:
        void ReleaseFrameInputBuffer(Frame& frame);
        void ProcessEncodedFrame(int32 bufferIndexToWrite, int64_t timestamp_us);
        NV_ENC_REGISTERED_PTR RegisterResource(NV_ENC_INPUT_RESOURCE_TYPE type, void *pBuffer);
        void MapResources(InputFrame& inputFrame);
        NV_ENC_OUTPUT_PTR InitializeBitstreamBuffer();
        NV_ENC_INITIALIZE_PARAMS nvEncInitializeParams = {};
        NV_ENC_CONFIG nvEncConfig = {};
        NVENCSTATUS errorCode;
        Frame bufferedFrames[bufferedFrameNum];
        ITexture2D* m_renderTextures[bufferedFrameNum] = {};
        std::vector<std::shared_ptr<void>> m_buffers;
        uint64 frameCount = 0;
        void* pEncoderInterface = nullptr;
        bool isIdrFrame = false;

        webrtc::Clock* m_clock;

        uint32_t m_frameRate = 30;
        uint32_t m_targetBitrate = 0;
        rtc::TimestampAligner timestamp_aligner_;
        // for active mode
        GLContext * ctx_ = nullptr;
        Scheduler *sched_ = nullptr;

        // for passive mode
        std::vector<std::function<void()>> requests_;
        std::shared_ptr<open3d::core::Tensor> frame_;
        std::mutex mt_;
        bool end_ = false;


        // for filament hook
        int64_t last_ts = 0;
        int64_t fps_ = 0;
        int64_t du_ = 0;
        std::atomic_int32_t waited_frame_{-1};
        int64_t waited_ts_ = 0;
    };

} // end namespace webrtc
} // end namespace unity