#pragma once
#include <cuda.h>
#include "open3d/visualization/webrtc_server/nvenc/NvCodec/include/nvEncodeAPI.h"

namespace unity
{
namespace webrtc
{

class ICudaDevice
{
public:
    virtual bool IsCudaSupport() = 0;
    virtual CUcontext GetCUcontext() = 0;
};
} // namespace webrtc
} // namespace unity
