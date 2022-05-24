// ----------------------------------------------------------------------------
// -                        Open3D: www.open3d.org                            -
// ----------------------------------------------------------------------------
// The MIT License (MIT)
//
// Copyright (c) 2018-2021 www.open3d.org
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in
// all copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
// FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
// IN THE SOFTWARE.
// ----------------------------------------------------------------------------

#include "open3d/visualization/webrtc_server/ImageCapturer.h"

#include <api/video/i420_buffer.h>
#include <libyuv/convert.h>
#include <libyuv/video_common.h>
#include <media/base/video_broadcaster.h>
#include <media/base/video_common.h>

#include <memory>
#include "open3d/visualization/webrtc_server/WebRTCWindowSystem.h"

#include "open3d/core/Tensor.h"
#include "open3d/utility/Logging.h"
#ifdef USE_NVENC
#include "open3d/visualization/webrtc_server/nvenc/pch.h"
#include "open3d/visualization/webrtc_server/nvenc/DummyVideoEncoder.h"
#include "open3d/visualization/webrtc_server/NvEncodeImpl.h"
#include "open3d/visualization/webrtc_server/nvenc/NvCodec/codec/NvEncoder.h"
#endif
namespace open3d {
namespace visualization {
namespace webrtc_server {

ImageCapturer::ImageCapturer(const std::string& url,
                             const std::map<std::string, std::string>& opts)
    :  width_(0), height_(0), url_(url) {
    if (opts.find("width") != opts.end()) {
        width_ = std::stoi(opts.at("width"));
    }
    if (opts.find("height") != opts.end()) {
        height_ = std::stoi(opts.at("height"));
    }
#ifdef USE_NVENC
    if (url_.empty()) {
        return;
    }
    auto w = WebRTCWindowSystem::GetInstance()->GetOSWindowByUID(url_);
    if(w == nullptr) {
        return;
    }
    auto sz = WebRTCWindowSystem::GetInstance()->GetWindowSize(w);
    if(sz.width == 0 || sz.height == 0) {
        return;
    }
    auto o3d = WebRTCWindowSystem::GetInstance()->GetO3DWindow(w);
    NvEncoderImpl::GetInstance()->AddEncoder(sz.width, sz.height, o3d, [this](auto encoder){
        encoder->CaptureFrame.connect(this, &ImageCapturer::DelegateFrame);
        impl_id_.store(encoder->Id());
    });
#endif
}

    ImageCapturer::~ImageCapturer() {
#ifdef USE_NVENC
        if (impl_id_.load() > 0) {
        NvEncoderImpl::GetInstance()->RemoveEncoder(impl_id_.load());
        impl_id_ = 0;
    }
#endif
}

ImageCapturer* ImageCapturer::Create(
        const std::string& url,
        const std::map<std::string, std::string>& opts) {
    std::unique_ptr<ImageCapturer> image_capturer(new ImageCapturer(url, opts));
    return image_capturer.release();
}

#ifdef USE_NVENC
void ImageCapturer::OnCaptureResult(
        const std::shared_ptr<core::Tensor>& frame) {
//    static long cnt = 0;
//    std::cout << "encode frame " << cnt++ << std::endl;
    auto impl = NvEncoderImpl::GetInstance();
    if (impl_id_.load() > 0) {
        impl->EncodeFrame(impl_id_, frame);
    } else {
#if CTX_TYPE == CTX_FILAMENT
        auto w = WebRTCWindowSystem::GetInstance()->GetOSWindowByUID(url_);
        WebRTCWindowSystem::GetInstance()->PostRedrawEvent(w);
#endif
    }
}
void ImageCapturer::DelegateFrame(const webrtc::VideoFrame &frame) {
    broadcaster_.OnFrame(frame);
}

#else
void ImageCapturer::OnCaptureResult(
        const std::shared_ptr<core::Tensor>& frame) {
    int height = (int)frame->GetShape(0);
    int width = (int)frame->GetShape(1);

    rtc::scoped_refptr<webrtc::I420Buffer> i420_buffer =
            webrtc::I420Buffer::Create(width, height);

    // frame->data()
    const int conversion_result = libyuv::ConvertToI420(
            frame->GetDataPtr<uint8_t>(), 0, i420_buffer->MutableDataY(),
            i420_buffer->StrideY(), i420_buffer->MutableDataU(),
            i420_buffer->StrideU(), i420_buffer->MutableDataV(),
            i420_buffer->StrideV(), 0, 0, width, height, i420_buffer->width(),
            i420_buffer->height(), libyuv::kRotate0, ::libyuv::FOURCC_RAW);

    if (conversion_result >= 0) {
        webrtc::VideoFrame video_frame(i420_buffer,
                                       webrtc::VideoRotation::kVideoRotation_0,
                                       rtc::TimeMicros());
        if ((height_ == 0) && (width_ == 0)) {
            broadcaster_.OnFrame(video_frame);
        } else {
            int height = height_;
            int width = width_;
            if (height == 0) {
                height = (video_frame.height() * width) / video_frame.width();
            } else if (width == 0) {
                width = (video_frame.width() * height) / video_frame.height();
            }
            int stride_y = width;
            int stride_uv = (width + 1) / 2;
            rtc::scoped_refptr<webrtc::I420Buffer> scaled_buffer =
                    webrtc::I420Buffer::Create(width, height, stride_y,
                                               stride_uv, stride_uv);
            scaled_buffer->ScaleFrom(
                    *video_frame.video_frame_buffer()->ToI420());
            webrtc::VideoFrame frame = webrtc::VideoFrame(
                    scaled_buffer, webrtc::kVideoRotation_0, rtc::TimeMicros());

            broadcaster_.OnFrame(frame);
        }
    } else {
        utility::LogError("ImageCapturer:OnCaptureResult conversion error: {}",
                          conversion_result);
    }
}
#endif

// Overide rtc::VideoSourceInterface<webrtc::VideoFrame>.
void ImageCapturer::AddOrUpdateSink(
        rtc::VideoSinkInterface<webrtc::VideoFrame>* sink,
        const rtc::VideoSinkWants& wants) {
    broadcaster_.AddOrUpdateSink(sink, wants);
}

void ImageCapturer::RemoveSink(
        rtc::VideoSinkInterface<webrtc::VideoFrame>* sink) {
    broadcaster_.RemoveSink(sink);
}

}  // namespace webrtc_server
}  // namespace visualization
}  // namespace open3d
