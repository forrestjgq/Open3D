#include "open3d/Open3D.h"

int main(int argc, char **argv) {
    using namespace open3d;

    if (argc < 2) {
        return 1;
    }

    visualization::VisualizerWithEditing vis(1);
    vis.CreateVisualizerWindow("VisualEdit", 1920, 1080, 100, 100);
    auto pcd_ptr = io::CreatePointCloudFromFile(argv[1]);
    if (pcd_ptr->IsEmpty()) {
        utility::LogWarning("Failed to read the point cloud.");
        return 1;
    }
    vis.AddGeometry(pcd_ptr);
    if (pcd_ptr->points_.size() > 5000000) {
        vis.GetRenderOption().point_size_ = 1.0;
    }
    vis.Run();
    vis.DestroyVisualizerWindow();
    return 0;
}
