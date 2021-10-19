add_library(Open3D)

target_sources(Open3D PRIVATE
    Timer.cpp
)
target_compile_options(Open3D PRIVATE -fsycl -fsycl-unnamed-lambda)
target_link_libraries(Open3D PRIVATE sycl -fsycl)

open3d_set_global_properties(Open3D)

include(CMakePackageConfigHelpers)




