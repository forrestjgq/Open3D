#pragma once

#include "open3d/visualization/webrtc_server/nvenc/GraphicsDevice/ITexture2D.h"
//#include "WebRTCMacros.h"

namespace unity
{
namespace webrtc
{

struct OpenGLTexture2D : ITexture2D {
public:
    OpenGLTexture2D(uint32_t w, uint32_t h, GLuint tex);
    virtual ~OpenGLTexture2D();

    inline virtual void* GetNativeTexturePtrV();
    inline virtual const void* GetNativeTexturePtrV() const;
    inline virtual void* GetEncodeTexturePtrV();
    inline virtual const void* GetEncodeTexturePtrV() const;

    void CreatePBO();
    size_t GetBufferSize() const { return m_width * m_height * 4; }
    size_t GetPitch() const { return m_width * 4; }
    byte* GetBuffer() const { return m_buffer;  }
    GLuint GetPBO() const { return m_pbo; }
private:
    GLuint m_texture = 0;
    GLuint m_pbo = 0;
    byte* m_buffer = nullptr;
};

//---------------------------------------------------------------------------------------------------------------------

// todo(kazuki):: fix workaround
//#pragma clang diagnostic push
//#pragma clang diagnostic ignored "-Wint-to-void-pointer-cast"
void* OpenGLTexture2D::GetNativeTexturePtrV() {
    long v = m_texture;
    return (void*)v;
}
const void* OpenGLTexture2D::GetNativeTexturePtrV() const {
    long v = m_texture;
    return (void*)v;
};
void* OpenGLTexture2D::GetEncodeTexturePtrV() {
    long v = m_texture;
    return (void*)v;
}
const void* OpenGLTexture2D::GetEncodeTexturePtrV() const {
    long v = m_texture;
    return (void*)v;
}
//#pragma clang diagnostic pop

} // end namespace webrtd
} // end namespace unity