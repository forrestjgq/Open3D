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

#pragma once

#include <Eigen/Core>
#include <functional>
#include <memory>
#include <vector>

#include "open3d/visualization/gui/Widget.h"

namespace open3d {

namespace geometry {
class PointCloud;
class Image;
}

namespace visualization {
namespace rendering {
class Open3DScene;
class Camera;
}

namespace gui {
class Editor {
public:
    enum class SelectionType { None, Rectangle, Polygon, Circle};
    using Target = std::shared_ptr<const geometry::PointCloud>;
    explicit Editor(rendering::Open3DScene* scene);
    ~Editor() = default;

    void Start(Target target, std::function<void(bool)> selectionCallback);
    void Stop();

    std::vector<size_t> CollectSelectedIndices();
    Widget::EventResult Mouse(const MouseEvent& e);
    Widget::DrawResult Draw(const DrawContext& context, const Rect &frame);

private:
    bool SetSelection(SelectionType type);
    void CheckEditable();
    bool AllowEdit();
    bool Started();
    void UpdatePolygonPoint(int x, int y);
    bool CheckPolygonPoint(int x, int y);
    void AddPoint(int x, int y);
    Eigen::Vector2f PointAt(int i);
    Eigen::Vector2f PointAt(int i, int x, int y);
    std::vector<size_t> CropPolygon();
    std::vector<size_t> CropRectangle();
    std::vector<size_t> CropCircle();
private:
    rendering::Open3DScene* scene_;
    rendering::Camera* camera_;
    Target target_;
    bool editable_ = false;
    std::function<void(bool)> callback_;
    std::vector<Eigen::Vector2i> selection_;
    SelectionType type_ = SelectionType::None;
};
}  // namespace gui
}  // namespace visualization
}  // namespace open3d
