open3d_ispc_add_library(webrtc_server OBJECT)
if(BUILD_CUDA_MODULE)


    find_package(CUDA REQUIRED)
    target_link_libraries(Open3D PRIVATE cuda)
    add_definitions(-D_DEBUG=0)
    add_definitions(-DUSE_NVENC -DUNITY_LINUX)
    set(nvenc_src
        NvEncoderImpl.cpp
        nvenc/NvCodec/codec/NvEncoder.cpp
        nvenc/NvCodec/codec/NvEncoderGL.cpp
        nvenc/builtin_video_encoder_factory.cc
        nvenc/DummyVideoEncoder.cpp
        nvenc/DummyVideoEncoder.h
        nvenc/internal_encoder_factory.cc
        nvenc/pch.cpp
        nvenc/Logger.cpp
        nvenc/UnityVideoEncoderFactory.cpp
        nvenc/GraphicsDevice/IGraphicsDevice.cpp
        nvenc/GraphicsDevice/ITexture2D.cpp
        nvenc/GraphicsDevice/OpenGL/OpenGLTexture2D.cpp
        nvenc/GraphicsDevice/OpenGL/OpenGLTexture2D.h
        nvenc/GraphicsDevice/OpenGL/OpenGLGraphicsDevice.cpp
        nvenc/GraphicsDevice/Cuda/CudaContext.cpp
        nvenc/Dynlink/CMakeLists.txt
        nvenc/Dynlink/libcuda.so.init.c
        nvenc/Dynlink/libcuda.so.tramp.S
        nvenc/Dynlink/libnvcuvid.so.init.c
        nvenc/Dynlink/libnvcuvid.so.tramp.S
        nvenc/Dynlink/libnvidia-encode.so.init.c
        nvenc/Dynlink/libnvidia-encode.so.tramp.S
    )
    set(CTX_USAGE "egl")
    add_definitions(-DCTX_GLUT=1 -DCTX_EGL=2)
    if(${CTX_USAGE} STREQUAL "glut")
        find_package(GLUT REQUIRED)
        target_link_libraries(Open3D PRIVATE glut)
        add_definitions(-DCTX_TYPE=1)
    elseif(${CTX_USAGE} STREQUAL "egl")
        list(APPEND nvenc_src nvenc/NvCodec/codec/PlatformEGLHeadless.cpp)
        add_definitions(-DCTX_TYPE=2)
    endif()

    set(SCHED_MODE "passive")
#    set(SCHED_MODE "active")
    add_definitions(-DPASSIVE_MODE=1 -DACTIVE_MODE=2)
    if(${SCHED_MODE} STREQUAL "passive")
        add_definitions(-DSCHED_MODE=1)
    elseif(${SCHED_MODE} STREQUAL "active")
        add_definitions(-DSCHED_MODE=2)
    endif()
else()
    # NVENC is not supported
    set(nvenc_src)
endif()

target_sources(webrtc_server PRIVATE
    BitmapTrackSource.cpp
    HttpServerRequestHandler.cpp
    ImageCapturer.cpp
    PeerConnectionManager.cpp
    WebRTCWindowSystem.cpp
    ${nvenc_src}
)


target_compile_definitions(webrtc_server PRIVATE
    $<$<PLATFORM_ID:Linux,Darwin>:WEBRTC_POSIX>
    $<$<PLATFORM_ID:Darwin>:WEBRTC_MAC>
    $<$<PLATFORM_ID:Windows>:WEBRTC_WIN>
    _FILE_OFFSET_BITS=64 # for civetweb
    _LARGEFILE_SOURCE=1  # for civetweb
)
add_dependencies(webrtc_server copy_html_dir)

open3d_show_and_abort_on_warning(webrtc_server)
open3d_set_global_properties(webrtc_server)
open3d_set_open3d_lib_properties(webrtc_server)
open3d_link_3rdparty_libraries(webrtc_server)

if (NOT GUI_RESOURCE_DIR)
    message(FATAL_ERROR "GUI_RESOURCE_DIR is not defined.")
endif()
message(STATUS "Copying ${CMAKE_CURRENT_SOURCE_DIR}/html to ${GUI_RESOURCE_DIR}.")
file(MAKE_DIRECTORY ${GUI_RESOURCE_DIR})

# Force update ${GUI_RESOURCE_DIR}/html every time.
add_custom_target(copy_html_dir ALL
    COMMAND ${CMAKE_COMMAND} -E rm -rf
        ${GUI_RESOURCE_DIR}/html
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${CMAKE_CURRENT_SOURCE_DIR}/html
        ${GUI_RESOURCE_DIR}/html
)
