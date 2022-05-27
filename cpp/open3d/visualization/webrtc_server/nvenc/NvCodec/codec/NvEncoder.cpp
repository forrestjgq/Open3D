
#if CTX_TYPE == CTX_GLUT
#include <GL/glew.h>
#include <GL/glut.h>
#include <GL/freeglut_ext.h>
#elif CTX_TYPE == CTX_EGL
#include "open3d/visualization/webrtc_server/nvenc/NvCodec/codec/PlatformEGLHeadless.h"
#endif
#include "open3d/visualization/webrtc_server/nvenc/pch.h"
#include "NvEncoder.h"
#include <cstring>
#include "open3d/visualization/webrtc_server/nvenc/GraphicsDevice/IGraphicsDevice.h"
#include "open3d/visualization/webrtc_server/nvenc/GraphicsDevice/ITexture2D.h"
#include "open3d/visualization/webrtc_server/nvenc/DummyVideoEncoder.h"
#include "open3d/utility/Logging.h"
#include <iostream>
#if _WIN32
#else
#include <dlfcn.h>
#endif
#include "open3d/visualization/webrtc_server/nvenc/NvCodec/codec/thread_pool.h"
#include "open3d/visualization/webrtc_server/WebRTCWindowSystem.h"

namespace unity
{
namespace webrtc
{

    static void* s_hModule = nullptr;
    static std::unique_ptr<NV_ENCODE_API_FUNCTION_LIST> pNvEncodeAPI = nullptr;
    using Call = std::function<void ()>;

    class Scheduler {
    public:
        Scheduler() = default;
        virtual ~Scheduler() = default;
        virtual void doAsync(Call &&f) = 0;
    };
    class StationScheduler: public Scheduler {
    public:
        StationScheduler() {
            station_ = std::make_unique<vega::DoableStation>("NvEncoder");
        }
        ~StationScheduler() override = default;
        void doAsync(Call &&f) override {
            station_->doAsync(f);
        }

    private:
        std::unique_ptr<vega::DoableStation> station_;
    };

    class GLContext {
    public:
        GLContext(int w, int h): width_(w), height_(h) {}
        virtual ~GLContext() = default;
        virtual void create() = 0;
        virtual void makeCurrent() = 0;
        int width_ = 0;
        int height_ = 0;
    };

#if CTX_TYPE == CTX_EGL
    class EglContext : public GLContext {
    public:
        EglContext(int w, int h): GLContext(w, h) {

        }
        ~EglContext() override {
            if (egl_) {
                egl_->terminate();
                egl_.reset();
            }
        }
        void create() override {
            egl_ = std::make_shared<open3d::filament::PlatformEGLHeadless>();
            if (!egl_->createDriver(nullptr)) {
                std::cout << "egl drv create failure" << std::endl;
                egl_.reset();
            }
        }
        void makeCurrent() override {
            if (egl_) {
                egl_->makeCurrent();
            }
        }
        std::shared_ptr<open3d::filament::PlatformEGLHeadless> egl_;
    };
    using CtxType = EglContext;
#endif
#if CTX_TYPE == CTX_GLUT
    class GlutContext : public GLContext {
    public:
        GlutContext(int w, int h): GLContext(w, h) {
        }
        ~GlutContext() override {
            if (m_window) glutDestroyWindow(m_window);
        }
        int m_window = 0;
        void create() override {
            int argc = 1;
            char *argv[] = {strdup("hello")};
            glutInit(&argc, argv);
            glutInitDisplayMode(GLUT_RGBA | GLUT_SINGLE);
            glutInitWindowSize(width_, height_);
            int window = glutCreateWindow("Lego");
            if (!window)
            {
                std::cout << "Unable to create GLUT window." << std::endl;
                return;
            }
            glutHideWindow();
            m_window = window;
        }
        void makeCurrent() override {
            if (m_window) {
                if (m_window != glutGetWindow()) {
                    glutSetWindow(m_window);
                }
            }
        }
    };
    using CtxType = GlutContext;
#endif
#if CTX_TYPE == CTX_FILAMENT
    class FilamentContext : public GLContext {
    public:
        FilamentContext(int w, int h): GLContext(w, h) {
        }
        ~FilamentContext() override {
        }
        void create() override { }
        void makeCurrent() override { }
    };
    using CtxType = FilamentContext;
#endif

    NvEncoder::NvEncoder(
        const NV_ENC_DEVICE_TYPE type,
        const NV_ENC_INPUT_RESOURCE_TYPE inputType,
        const NV_ENC_BUFFER_FORMAT bufferFormat,
        const int width,
        const int height,
        IGraphicsDevice* device,
        UnityRenderingExtTextureFormat textureFormat)
    : m_width(width)
    , m_height(height)
    , m_device(device)
    , m_textureFormat(textureFormat)
    , m_deviceType(type)
    , m_inputType(inputType)
    , m_bufferFormat(bufferFormat)
    , m_clock(webrtc::Clock::GetRealTimeClock())
    {
        auto fps = open3d::visualization::webrtc_server::WebRTCWindowSystem::GetInstance()->GetMaxRenderFPS();
        if (fps > 0) {
            m_frameRate = fps;
        }
#if CTX_TYPE == CTX_FILAMENT
        fps_ = m_frameRate;
        du_ = 1000000/fps_;
        open3d::utility::LogInfo("NvEncoder FPS {}", fps_);
#endif

        sched_ = new StationScheduler();
    }
    void NvEncoder::InitInternal() {
#if SCHED_MODE == PASSIVE_MODE
        sched_->doAsync([this](){ this->WorkInPassive(); });
#elif SCHED_MODE == ACTIVE_MODE
        doInContext([this](){ this->InitV(); });
#elif SCHED_MODE == HOOK_MODE
        InitV();
#endif
    }

        bool NvEncoder::Keep() {
            int64_t now = m_clock->TimeInMicroseconds();
            if (last_ts > 0) {
                auto diff = now - last_ts;
                if (diff < du_) {
                    return false;
                }
//                open3d::utility::LogInfo("keep for du {}ms", diff/1000);
            }
            last_ts = now;
            return true;
        }
        void NvEncoder::InitV()
        {
            ctx_ = new CtxType(m_width, m_height);
        ctx_->create();
        bool result = true;
        if (m_initializationResult == CodecInitializationResult::NotInitialized)
        {
            m_initializationResult = LoadCodec();
        }
        if(m_initializationResult != CodecInitializationResult::Success)
        {
            throw m_initializationResult;
        }
//#pragma region open an encode session
        //open an encode session
        NV_ENC_OPEN_ENCODE_SESSION_EX_PARAMS openEncodeSessionExParams = {};
        openEncodeSessionExParams.version = NV_ENC_OPEN_ENCODE_SESSION_EX_PARAMS_VER;

        openEncodeSessionExParams.device = m_device->GetEncodeDevicePtrV();
        openEncodeSessionExParams.deviceType = m_deviceType;
        openEncodeSessionExParams.apiVersion = NVENCAPI_VERSION;

        errorCode = pNvEncodeAPI->nvEncOpenEncodeSessionEx(&openEncodeSessionExParams, &pEncoderInterface);
        open3d::utility::LogInfo("open encoder error {}", errorCode);
        checkf(NV_RESULT(errorCode), "Unable to open NvEnc encode session");
//#pragma endregion
//#pragma region set initialization parameters
        nvEncInitializeParams.version = NV_ENC_INITIALIZE_PARAMS_VER;
        nvEncInitializeParams.encodeWidth = m_width;
        nvEncInitializeParams.encodeHeight = m_height;
        nvEncInitializeParams.darWidth = m_width;
        nvEncInitializeParams.darHeight = m_height;
        nvEncInitializeParams.encodeGUID = NV_ENC_CODEC_H264_GUID;

        open3d::utility::LogInfo("init encoder w {} h {}", m_width, m_height);
// todo(kazuki):: fix workaround
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
        nvEncInitializeParams.presetGUID = NV_ENC_PRESET_LOW_LATENCY_HQ_GUID;
#pragma GCC diagnostic pop

        nvEncInitializeParams.frameRateNum = m_frameRate;
        nvEncInitializeParams.frameRateDen = 1;
        nvEncInitializeParams.enablePTD = 1;
        nvEncInitializeParams.reportSliceOffsets = 0;
        nvEncInitializeParams.enableSubFrameWrite = 0;
        nvEncInitializeParams.encodeConfig = &nvEncConfig;

        // Note:: Encoder will not allow dynamic resolution change.
        // Please set values if you want to support dynamic resolution change.
        nvEncInitializeParams.maxEncodeWidth = 0;
        nvEncInitializeParams.maxEncodeHeight = 0;
//#pragma endregion
//#pragma region get preset config and set it
        NV_ENC_PRESET_CONFIG presetConfig = {};
        presetConfig.version = NV_ENC_PRESET_CONFIG_VER;
        presetConfig.presetCfg.version = NV_ENC_CONFIG_VER;
        errorCode = pNvEncodeAPI->nvEncGetEncodePresetConfig(pEncoderInterface, nvEncInitializeParams.encodeGUID, nvEncInitializeParams.presetGUID, &presetConfig);
        checkf(NV_RESULT(errorCode), "Failed to select NVEncoder preset config");
        std::memcpy(&nvEncConfig, &presetConfig.presetCfg, sizeof(NV_ENC_CONFIG));
        nvEncConfig.profileGUID = NV_ENC_H264_PROFILE_BASELINE_GUID;
        nvEncConfig.gopLength = nvEncInitializeParams.frameRateNum / 2;
        nvEncConfig.rcParams.rateControlMode = NV_ENC_PARAMS_RC_CBR_LOWDELAY_HQ;
        nvEncConfig.rcParams.averageBitRate =
            (static_cast<unsigned int>(5.0f *
            nvEncInitializeParams.encodeWidth *
            nvEncInitializeParams.encodeHeight) / (m_width * m_height)) * 100000;
        nvEncConfig.encodeCodecConfig.h264Config.idrPeriod = nvEncConfig.gopLength;

        nvEncConfig.encodeCodecConfig.h264Config.sliceMode = 0;
        nvEncConfig.encodeCodecConfig.h264Config.sliceModeData = 0;
        nvEncConfig.encodeCodecConfig.h264Config.repeatSPSPPS = 1;
        //Quality Control
//        nvEncConfig.encodeCodecConfig.h264Config.level = NV_ENC_LEVEL_AUTOSELECT;
//        nvEncConfig.encodeCodecConfig.h264Config.level = NV_ENC_LEVEL_H264_51;
          nvEncConfig.encodeCodecConfig.h264Config.level = NV_ENC_LEVEL_H264_52;
//#pragma endregion
//#pragma region get encoder capability
        NV_ENC_CAPS_PARAM capsParam = {};
        capsParam.version = NV_ENC_CAPS_PARAM_VER;
        capsParam.capsToQuery = NV_ENC_CAPS_ASYNC_ENCODE_SUPPORT;
        int32 asyncMode = 0;
        errorCode = pNvEncodeAPI->nvEncGetEncodeCaps(pEncoderInterface, nvEncInitializeParams.encodeGUID, &capsParam, &asyncMode);
        checkf(NV_RESULT(errorCode), "Failed to get NVEncoder capability params");
        nvEncInitializeParams.enableEncodeAsync = 0;
//#pragma endregion
//#pragma region initialize hardware encoder session
        errorCode = pNvEncodeAPI->nvEncInitializeEncoder(pEncoderInterface, &nvEncInitializeParams);
        result = NV_RESULT(errorCode);
        checkf(result, "Failed to initialize NVEncoder");
//#pragma endregion
        InitEncoderResources();
        m_isNvEncoderSupported = true;
    }

    void NvEncoder::Release() {
        ReleaseEncoderResources();
        if (pEncoderInterface) {
            errorCode = pNvEncodeAPI->nvEncDestroyEncoder(pEncoderInterface);
            checkf(NV_RESULT(errorCode), "Failed to destroy NV encoder interface");
            pEncoderInterface = nullptr;
        }
        delete ctx_;
        ctx_ = nullptr;
    }
    NvEncoder::~NvEncoder()
    {
#if SCHED_MODE == PASSIVE_MODE
        doInContext([this](){ this->end_ = true;});
#elif SCHED_MODE == ACTIVE_MODE
        doInContext([this](){ this->Release(); });
#endif
        delete sched_;
        sched_ = nullptr;
    }
    void NvEncoder::WorkInPassive() {
        InitV();

        int64_t ts = 0;
        int64_t rate = m_frameRate;
        int64_t du = 1000000/rate;
        const int64_t sleep_ms = 5;
        while (!end_) {
            decltype(frame_) frame;
            decltype(requests_) requests;
            {
                std::unique_lock<std::mutex> lock(mt_);
                requests = std::move(requests_);
            }
            for (auto &r: requests) {
                r();
            }
            requests.clear();

            int64_t now = m_clock->TimeInMicroseconds();
            if (ts > 0) {
                auto diff = now - ts;
//                std::cout << "diff " << diff << " du " << du << std::endl;
                if (diff < du && du - diff > sleep_ms * 1000) {
                    // we have enough time to wait
//                    std::cout << "sleep" << std::endl;
                    std::this_thread::sleep_for(std::chrono::milliseconds(sleep_ms));
                    continue;
                }
            }
            ts = now;
            {
                std::unique_lock<std::mutex> lock(mt_);
                frame = frame_;
                frame_.reset();
            }
            if (frame) {
                if (!CopyBufferFromCPU(frame->GetDataPtr())) {
                    open3d::utility::LogInfo("fail to copy frame from CPU");
                } else {
                    // ts should be updated
                    ts = m_clock->TimeInMicroseconds();
                }
            }
//            std::cout << "encode at " << ts << std::endl;
            EncodeFrame(ts);
            frameCount--;
        }
        Release();
    }

    void NvEncoder::doInContext(std::function<void()> &&f) {
#if SCHED_MODE == PASSIVE_MODE || SCHED_MODE == HOOK_MODE
        std::unique_lock<std::mutex> lock(mt_);
        requests_.push_back(f);
#elif SCHED_MODE == ACTIVE_MODE
        sched_->doAsync(std::move(f));
#endif
    }
#define CHECK_ERR() do { \
    auto err = glGetError(); \
    if (err != 0) {   \
            std::cout << __FILE__ << ":" << __LINE__ << ": opengl error " << err << std::endl; \
}\
    } while(0)
    void NvEncoder::WaitFrameDone() {
        auto idx = waited_frame_.exchange(-1);
        if(idx >= 0) {
//            std::cout << "wait done " << std::this_thread::get_id() << std::endl;
            ProcessEncodedFrame(idx, waited_ts_);
        }
    }
    void NvEncoder::RecvFromFilament(void *fbo, void *pbo) {
        if(!Keep()) {
            return;
        }
//        std::cout << "recv frame" << std::this_thread::get_id() << std::endl;
        decltype(requests_) requests;
        {
            std::unique_lock<std::mutex> lock(mt_);
            requests = std::move(requests_);
        }
        for (auto &r: requests) {
            r();
        }
        requests.clear();
        const int curFrameNum = GetCurrentFrameCount() % bufferedFrameNum;
        const auto tex = m_renderTextures[curFrameNum];
        if (tex == nullptr)
            return;
        const GLuint dstName = reinterpret_cast<uintptr_t>(tex->GetNativeTexturePtrV());
//        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,GL_TEXTURE_2D, dstName, 1);
        glBindTexture(GL_TEXTURE_2D, dstName);
        glBindBuffer(GL_PIXEL_UNPACK_BUFFER, (GLuint)(long)pbo);
        glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0,
                        m_width, m_height,
                        GL_RGB, GL_UNSIGNED_BYTE, nullptr);
        glBindTexture(GL_TEXTURE_2D, 0);
        glBindBuffer(GL_PIXEL_UNPACK_BUFFER, 0);
        int64_t timestamp_us = m_clock->TimeInMicroseconds();
        auto idx = EncodeFrameEx(timestamp_us);
        waited_frame_.store(idx);
        waited_ts_ = timestamp_us;
        // call in another thread
//        sched_->doAsync([=](){ this->ProcessEncodedFrame(idx, timestamp_us);});
    }
    void NvEncoder::RecvOpen3DCPUFrame(const std::shared_ptr<open3d::core::Tensor>& frame) {
#if SCHED_MODE == PASSIVE_MODE
        std::unique_lock<std::mutex> lock(mt_);
        frame_ = frame;
#elif SCHED_MODE == ACTIVE_MODE
        doInContext([this, frame](){
            if(CopyBufferFromCPU(frame->GetDataPtr())) {
                int64_t timestamp_us = m_clock->TimeInMicroseconds();
                EncodeFrame(timestamp_us);
            }
        });
#elif SCHED_MODE == HOOK_MODE
        assert(false);
#endif
    }

    CodecInitializationResult NvEncoder::LoadCodec()
    {
        pNvEncodeAPI = std::make_unique<NV_ENCODE_API_FUNCTION_LIST>();
        pNvEncodeAPI->version = NV_ENCODE_API_FUNCTION_LIST_VER;

        if (!LoadModule()) {
            open3d::utility::LogError("Load module fail");
        }

        if(!CheckDriverVersion()) {
            open3d::utility::LogError("Check driver version fail");
        }

        using NvEncodeAPICreateInstance_Type = NVENCSTATUS(NVENCAPI *)(NV_ENCODE_API_FUNCTION_LIST*);
#if defined(_WIN32)
        NvEncodeAPICreateInstance_Type NvEncodeAPICreateInstance = (NvEncodeAPICreateInstance_Type)GetProcAddress((HMODULE)s_hModule, "NvEncodeAPICreateInstance");
#else
        NvEncodeAPICreateInstance_Type NvEncodeAPICreateInstance = (NvEncodeAPICreateInstance_Type)dlsym(s_hModule, "NvEncodeAPICreateInstance");
#endif

        if (!NvEncodeAPICreateInstance)
        {
            open3d::utility::LogError("Cannot find NvEncodeAPICreateInstance() entry in NVENC library");
            return CodecInitializationResult::APINotFound;
        }
        bool result = (NvEncodeAPICreateInstance(pNvEncodeAPI.get()) == NV_ENC_SUCCESS);
        checkf(result, "Unable to create NvEnc API function list");
        if (!result) {
            return CodecInitializationResult::APINotFound;
        }
        return CodecInitializationResult::Success;
    }

    bool NvEncoder::CheckDriverVersion()
    {
        using NvEncodeAPIGetMaxSupportedVersion_Type = NVENCSTATUS(NVENCAPI*)(uint32_t*);
#if defined(_WIN32)
        NvEncodeAPIGetMaxSupportedVersion_Type NvEncodeAPIGetMaxSupportedVersion =
            (NvEncodeAPIGetMaxSupportedVersion_Type)GetProcAddress((HMODULE)s_hModule, "NvEncodeAPIGetMaxSupportedVersion");
#else
        NvEncodeAPIGetMaxSupportedVersion_Type NvEncodeAPIGetMaxSupportedVersion =
            (NvEncodeAPIGetMaxSupportedVersion_Type)dlsym(s_hModule, "NvEncodeAPIGetMaxSupportedVersion");
#endif

        uint32_t version = 0;
        uint32_t currentVersion = (NVENCAPI_MAJOR_VERSION << 4) | NVENCAPI_MINOR_VERSION;
        NvEncodeAPIGetMaxSupportedVersion(&version);
        if (currentVersion > version)
        {
            open3d::utility::LogError("Current Driver Version does not support this NvEncodeAPI version, please upgrade driver");
            return false;
        }
        return true;
    }

    bool NvEncoder::LoadModule()
    {
        if (s_hModule != nullptr)
            return true;

#if defined(_WIN32)
#if defined(_WIN64)
        HMODULE module = LoadLibrary(TEXT("nvEncodeAPI64.dll"));
#else
        HMODULE module = LoadLibrary(TEXT("nvEncodeAPI.dll"));
#endif
#else
        void *module = dlopen("libnvidia-encode.so.1", RTLD_LAZY);
#endif

        if (module == nullptr)
        {
            open3d::utility::LogError("NVENC library file is not found. Please ensure NV driver is installed");
            return false;
        }
        s_hModule = module;
        return true;
    }

    void NvEncoder::UnloadModule()
    {
        if (s_hModule)
        {
#if defined(_WIN32)
            FreeLibrary((HMODULE)s_hModule);
#else
            dlclose(s_hModule);
#endif
            s_hModule = nullptr;
        }
    }

    void NvEncoder::UpdateSettings()
    {
        bool settingChanged = false;
        if (nvEncConfig.rcParams.averageBitRate != m_targetBitrate)
        {
            nvEncConfig.rcParams.averageBitRate = m_targetBitrate;
            settingChanged = true;
        }
        if (nvEncInitializeParams.frameRateNum != m_frameRate)
        {
            // NV_ENC_LEVEL_H264_51 do not allow a framerate over 120
            const uint32_t kMaxFramerate = 120;
            uint32_t targetFramerate = std::min(m_frameRate, kMaxFramerate);
            nvEncInitializeParams.frameRateNum = targetFramerate;
            settingChanged = true;
        }

        if (settingChanged)
        {
            NV_ENC_RECONFIGURE_PARAMS nvEncReconfigureParams;
            std::memcpy(&nvEncReconfigureParams.reInitEncodeParams, &nvEncInitializeParams, sizeof(nvEncInitializeParams));
            nvEncReconfigureParams.version = NV_ENC_RECONFIGURE_PARAMS_VER;
            errorCode = pNvEncodeAPI->nvEncReconfigureEncoder(pEncoderInterface, &nvEncReconfigureParams);
            if(!(NV_RESULT(errorCode))) {
                std::cout << "error code " << errorCode << " frame rate " << nvEncInitializeParams.frameRateNum << " bit rate " << m_targetBitrate << std::endl;
            }
            checkf(NV_RESULT(errorCode), "Failed to reconfigure encoder setting");
        }
    }

    void NvEncoder::SetIdrFrame()  {
        doInContext([=](){
            this->isIdrFrame = true;
        });
    }
    void NvEncoder::SetRates(uint32_t bitRate, int64_t frameRate){
//        std::cout << "biterate " << bitRate/(8*1024) << "KB frame rate " << frameRate << std::endl;
        doInContext([=](){
            this->m_frameRate = static_cast<uint32_t>(frameRate);
            this->m_targetBitrate = bitRate ;
            this->isIdrFrame = true;

        });
    }

bool NvEncoder::CopyBuffer(void* frame)
    {
        const int curFrameNum = GetCurrentFrameCount() % bufferedFrameNum;
        const auto tex = m_renderTextures[curFrameNum];
        if (tex == nullptr)
            return false;
        m_device->CopyResourceFromNativeV(tex, frame);
        return true;
    }
    bool NvEncoder::CopyBufferFromCPU(void* frame)
    {
        const int curFrameNum = GetCurrentFrameCount() % bufferedFrameNum;
        const auto tex = m_renderTextures[curFrameNum];
        if (tex == nullptr)
            return false;
#if 0
        auto idx = (GetCurrentFrameCount()+1) % bufferedFrameNum;
        auto ptr = (void *)(long)m_renderTextures[idx];
        return m_device->CopyResourceFromNativeV(tex, ptr);
#else
        auto b = m_device->CopyResourceFromCPU(tex, frame);
        if (b) {
//            glFlush();
            glFinish();
        }
        return b;
#endif
    }
    int32 NvEncoder::EncodeFrameEx(int64_t timestamp_us)
    {
        UpdateSettings();
        int32 bufferIndexToWrite = frameCount % bufferedFrameNum;
        Frame& frame = bufferedFrames[bufferIndexToWrite];
//#pragma region configure per-frame encode parameters
        NV_ENC_PIC_PARAMS picParams = {};
        picParams.version = NV_ENC_PIC_PARAMS_VER;
        picParams.pictureStruct = NV_ENC_PIC_STRUCT_FRAME;
//        picParams.pictureStruct = NV_ENC_PIC_STRUCT_FIELD_BOTTOM_TOP;
        picParams.inputBuffer = frame.inputFrame.mappedResource;
        picParams.bufferFmt = frame.inputFrame.bufferFormat;
        picParams.inputWidth = nvEncInitializeParams.encodeWidth;
        picParams.inputHeight = nvEncInitializeParams.encodeHeight;
        picParams.outputBitstream = frame.outputFrame;
        picParams.inputTimeStamp = frameCount;
        if (isIdrFrame)
        {
            picParams.encodePicFlags = NV_ENC_PIC_FLAG_FORCEIDR | NV_ENC_PIC_FLAG_FORCEINTRA;
            isIdrFrame = false;
        }
        errorCode = pNvEncodeAPI->nvEncEncodePicture(pEncoderInterface, &picParams);
        checkf(NV_RESULT(errorCode), "Failed to encode frame");
        frameCount++;
        return bufferIndexToWrite;
    }

    //entry for encoding a frame
    bool NvEncoder::EncodeFrame(int64_t timestamp_us)
    {
        ProcessEncodedFrame(EncodeFrameEx(timestamp_us), timestamp_us);
        return true;
    }

    //get encoded frame
    void NvEncoder::ProcessEncodedFrame(int32 bufferIndexToWrite, int64_t timestamp_us)
    {
        Frame& frame = bufferedFrames[bufferIndexToWrite];
//#pragma region retrieve encoded frame from output buffer
        NV_ENC_LOCK_BITSTREAM lockBitStream = {};
        lockBitStream.version = NV_ENC_LOCK_BITSTREAM_VER;
        lockBitStream.outputBitstream = frame.outputFrame;
        lockBitStream.doNotWait = nvEncInitializeParams.enableEncodeAsync;
        errorCode = pNvEncodeAPI->nvEncLockBitstream(pEncoderInterface, &lockBitStream);
        checkf(NV_RESULT(errorCode), "Failed to lock bit stream");
        if (lockBitStream.bitstreamSizeInBytes)
        {
            frame.encodedFrame.resize(lockBitStream.bitstreamSizeInBytes);
            std::memcpy(frame.encodedFrame.data(), lockBitStream.bitstreamBufferPtr, lockBitStream.bitstreamSizeInBytes);
        }
        errorCode = pNvEncodeAPI->nvEncUnlockBitstream(pEncoderInterface, frame.outputFrame);
        checkf(NV_RESULT(errorCode), "Failed to unlock bit stream");
//#pragma endregion
        const rtc::scoped_refptr<FrameBuffer> buffer =
            new rtc::RefCountedObject<FrameBuffer>(
                m_width, m_height, frame.encodedFrame, m_encoderId);
        const int64_t now_us = rtc::TimeMicros();
        const int64_t translated_camera_time_us =
            timestamp_aligner_.TranslateTimestamp(
                timestamp_us,
                now_us);

        webrtc::VideoFrame::Builder builder =
            webrtc::VideoFrame::Builder()
            .set_video_frame_buffer(buffer)
            .set_timestamp_us(translated_camera_time_us)
            .set_timestamp_rtp(0)
            .set_ntp_time_ms(rtc::TimeMillis());

        CaptureFrame(builder.build());
    }

    NV_ENC_REGISTERED_PTR NvEncoder::RegisterResource(NV_ENC_INPUT_RESOURCE_TYPE inputType, void *buffer)
    {
        NV_ENC_REGISTER_RESOURCE registerResource = {};
        registerResource.version = NV_ENC_REGISTER_RESOURCE_VER;
        registerResource.resourceType = inputType;
        registerResource.resourceToRegister = buffer;

        if (!registerResource.resourceToRegister)
            RTC_LOG(LS_INFO) << "resource is not initialized";
        registerResource.width = m_width;
        registerResource.height = m_height;
        if (inputType != NV_ENC_INPUT_RESOURCE_TYPE_CUDAARRAY)
        {
            registerResource.pitch = GetWidthInBytes(m_bufferFormat, m_width);
        } else{
            registerResource.pitch = m_width;
        }
        registerResource.bufferFormat = m_bufferFormat;
        registerResource.bufferUsage = NV_ENC_INPUT_IMAGE;
        errorCode = pNvEncodeAPI->nvEncRegisterResource(pEncoderInterface, &registerResource);
        checkf(NV_RESULT(errorCode), "nvEncRegisterResource error " );
        return registerResource.registeredResource;
    }
    void NvEncoder::MapResources(InputFrame& inputFrame)
    {
        NV_ENC_MAP_INPUT_RESOURCE mapInputResource = {};
        mapInputResource.version = NV_ENC_MAP_INPUT_RESOURCE_VER;
        mapInputResource.registeredResource = inputFrame.registeredResource;
        errorCode = pNvEncodeAPI->nvEncMapInputResource(pEncoderInterface, &mapInputResource);
        checkf(NV_RESULT(errorCode), "nvEncMapInputResource error");
        inputFrame.mappedResource = mapInputResource.mappedResource;
    }
    NV_ENC_OUTPUT_PTR NvEncoder::InitializeBitstreamBuffer()
    {
        NV_ENC_CREATE_BITSTREAM_BUFFER createBitstreamBuffer = {};
        createBitstreamBuffer.version = NV_ENC_CREATE_BITSTREAM_BUFFER_VER;
        errorCode = pNvEncodeAPI->nvEncCreateBitstreamBuffer(pEncoderInterface, &createBitstreamBuffer);
        checkf(NV_RESULT(errorCode), "nvEncCreateBitstreamBuffer error");
        return createBitstreamBuffer.bitstreamBuffer;
    }
    void NvEncoder::InitEncoderResources()
    {
        for (uint32 i = 0; i < bufferedFrameNum; i++)
        {
            m_renderTextures[i] = m_device->CreateDefaultTextureV(m_width, m_height, m_textureFormat);
            auto buffer = AllocateInputResourceV(m_renderTextures[i]);
            m_buffers.push_back(buffer);
            Frame& frame = bufferedFrames[i];
            frame.inputFrame.registeredResource = RegisterResource(m_inputType, buffer.get());
            frame.inputFrame.bufferFormat = m_bufferFormat;
            MapResources(frame.inputFrame);
            frame.outputFrame = InitializeBitstreamBuffer();
        }
    }

    void NvEncoder::ReleaseFrameInputBuffer(Frame& frame)
    {
        if(frame.inputFrame.mappedResource)
        {
            errorCode = pNvEncodeAPI->nvEncUnmapInputResource(pEncoderInterface, frame.inputFrame.mappedResource);
            checkf(NV_RESULT(errorCode), "Failed to unmap input resource");
            frame.inputFrame.mappedResource = nullptr;
        }

        if(frame.inputFrame.registeredResource)
        {
            errorCode = pNvEncodeAPI->nvEncUnregisterResource(pEncoderInterface, frame.inputFrame.registeredResource);
            checkf(NV_RESULT(errorCode), "Failed to unregister input buffer resource");
            frame.inputFrame.registeredResource = nullptr;
        }

    }
    void NvEncoder::ReleaseEncoderResources() {
        for (Frame &frame : bufferedFrames) {
            ReleaseFrameInputBuffer(frame);
            if (frame.outputFrame != nullptr) {
                errorCode = pNvEncodeAPI->nvEncDestroyBitstreamBuffer(pEncoderInterface, frame.outputFrame);
                checkf(NV_RESULT(errorCode), "Failed to destroy output buffer bit stream");
                frame.outputFrame = nullptr;
            }
        }

        for (auto &renderTexture : m_renderTextures) {
            delete renderTexture;
            renderTexture = nullptr;
        }
        m_buffers.clear();
    }

    uint32_t NvEncoder::GetNumChromaPlanes(const NV_ENC_BUFFER_FORMAT bufferFormat)
    {
        switch (bufferFormat)
        {
            case NV_ENC_BUFFER_FORMAT_NV12:
            case NV_ENC_BUFFER_FORMAT_YUV420_10BIT:
                return 1;
            case NV_ENC_BUFFER_FORMAT_YV12:
            case NV_ENC_BUFFER_FORMAT_IYUV:
            case NV_ENC_BUFFER_FORMAT_YUV444:
            case NV_ENC_BUFFER_FORMAT_YUV444_10BIT:
                return 2;
            case NV_ENC_BUFFER_FORMAT_ARGB:
            case NV_ENC_BUFFER_FORMAT_ARGB10:
            case NV_ENC_BUFFER_FORMAT_AYUV:
            case NV_ENC_BUFFER_FORMAT_ABGR:
            case NV_ENC_BUFFER_FORMAT_ABGR10:
                return 0;
            default:
                return -1;
        }
    }
    uint32_t NvEncoder::GetChromaHeight(const NV_ENC_BUFFER_FORMAT bufferFormat, const uint32_t lumaHeight)
    {
        switch (bufferFormat)
        {
            case NV_ENC_BUFFER_FORMAT_YV12:
            case NV_ENC_BUFFER_FORMAT_IYUV:
            case NV_ENC_BUFFER_FORMAT_NV12:
            case NV_ENC_BUFFER_FORMAT_YUV420_10BIT:
                return (lumaHeight + 1)/2;
            case NV_ENC_BUFFER_FORMAT_YUV444:
            case NV_ENC_BUFFER_FORMAT_YUV444_10BIT:
                return lumaHeight;
            case NV_ENC_BUFFER_FORMAT_ARGB:
            case NV_ENC_BUFFER_FORMAT_ARGB10:
            case NV_ENC_BUFFER_FORMAT_AYUV:
            case NV_ENC_BUFFER_FORMAT_ABGR:
            case NV_ENC_BUFFER_FORMAT_ABGR10:
                return 0;
            default:
                return 0;
        }
    }
    uint32_t NvEncoder::GetWidthInBytes(const NV_ENC_BUFFER_FORMAT bufferFormat, const uint32_t width)
    {
        switch (bufferFormat) {
            case NV_ENC_BUFFER_FORMAT_NV12:
            case NV_ENC_BUFFER_FORMAT_YV12:
            case NV_ENC_BUFFER_FORMAT_IYUV:
            case NV_ENC_BUFFER_FORMAT_YUV444:
                return width;
            case NV_ENC_BUFFER_FORMAT_YUV420_10BIT:
            case NV_ENC_BUFFER_FORMAT_YUV444_10BIT:
                return width * 2;
            case NV_ENC_BUFFER_FORMAT_ARGB:
            case NV_ENC_BUFFER_FORMAT_ARGB10:
            case NV_ENC_BUFFER_FORMAT_AYUV:
            case NV_ENC_BUFFER_FORMAT_ABGR:
            case NV_ENC_BUFFER_FORMAT_ABGR10:
                return width * 4;
            default:
                return 0;
        }
    }

} // end namespace webrtc
} // end namespace unity