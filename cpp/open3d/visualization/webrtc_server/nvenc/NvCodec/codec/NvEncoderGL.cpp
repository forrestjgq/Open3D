#include "open3d/visualization/webrtc_server/nvenc/pch.h"
#include "open3d/visualization/webrtc_server/nvenc/NvCodec/include/nvEncodeAPI.h"
#include "NvEncoderGL.h"
#include "open3d/visualization/webrtc_server/nvenc/GraphicsDevice/IGraphicsDevice.h"
#include "open3d/visualization/webrtc_server/nvenc/GraphicsDevice/ITexture2D.h"

namespace unity
{
namespace webrtc
{

    NvEncoderGL::NvEncoderGL(uint32_t nWidth, uint32_t nHeight, IGraphicsDevice* device, UnityRenderingExtTextureFormat textureFormat) :
        NvEncoder(NV_ENC_DEVICE_TYPE_OPENGL, NV_ENC_INPUT_RESOURCE_TYPE_OPENGL_TEX, NV_ENC_BUFFER_FORMAT_ABGR, nWidth, nHeight, device, textureFormat) {
    }

    NvEncoderGL::~NvEncoderGL() = default;

    std::shared_ptr<void> NvEncoderGL::AllocateInputResourceV(ITexture2D* tex) {
        auto p = std::make_shared<NV_ENC_INPUT_RESOURCE_OPENGL_TEX>();
        p->texture = (GLuint)(size_t)(tex->GetEncodeTexturePtrV());
        p->target = GL_TEXTURE_2D;
        return p;
    }

} // end namespace webrtc
} // end namespace unity 
