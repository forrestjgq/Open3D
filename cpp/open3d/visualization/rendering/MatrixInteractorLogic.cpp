// ----------------------------------------------------------------------------
// -                        Open3D: www.open3d.org                            -
// ----------------------------------------------------------------------------
// The MIT License (MIT)
//
// Copyright (c) 2018 www.open3d.org
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

#include "open3d/visualization/rendering/MatrixInteractorLogic.h"
#include "open3d/utility/Console.h"

namespace open3d {
namespace visualization {
namespace rendering {

MatrixInteractorLogic::~MatrixInteractorLogic() {}

void MatrixInteractorLogic::SetViewSize(int width, int height) {
    view_width_ = width;
    view_height_ = height;
}

int MatrixInteractorLogic::GetViewWidth() const { return view_width_; }

int MatrixInteractorLogic::GetViewHeight() const { return view_height_; }

const geometry::AxisAlignedBoundingBox& MatrixInteractorLogic::GetBoundingBox()
        const {
    return model_bounds_;
}

void MatrixInteractorLogic::SetBoundingBox(
        const geometry::AxisAlignedBoundingBox& bounds) {
    model_size_ = (bounds.GetMaxBound() - bounds.GetMinBound()).norm();
    model_bounds_ = bounds;
}

Eigen::Vector3f MatrixInteractorLogic::GetCenterOfRotation() const {
    return center_of_rotation_;
}

void MatrixInteractorLogic::SetMouseDownInfo(
        const Camera::Transform& matrix,
        const Eigen::Vector3f& center_of_rotation) {
    matrix_ = matrix;
    center_of_rotation_ = center_of_rotation;

    matrix_at_mouse_down_ = matrix;
    center_of_rotation_at_mouse_down_ = center_of_rotation;
}

void MatrixInteractorLogic::SetMatrix(const Camera::Transform& matrix) {
    matrix_ = matrix;
}

const Camera::Transform& MatrixInteractorLogic::GetMatrix() const {
    return matrix_;
}

void MatrixInteractorLogic::Rotate(int dx, int dy) {
    auto matrix = matrix_at_mouse_down_;  // copy
    Eigen::AngleAxisf rot_matrix(0, Eigen::Vector3f(1, 0, 0));

    // We want to rotate as if we were rotating an imaginary trackball
    // centered at the point of rotation. To do this we need an axis
    // of rotation and an angle about the axis. To find the axis, we
    // imagine that the viewing plane has been translated into the screen
    // so that it intersects the center of rotation. The axis we want
    // to rotate around is perpendicular to the vector defined by (dx, dy)
    // (assuming +x is right and +y is up). (Imagine the situation if the
    // mouse movement is (100, 0) or (0, 100).) Now it is easy to find
    // the perpendicular in 2D. Conveniently, (axis.x, axis.y, 0) is the
    // correct axis in camera-local coordinates. We can multiply by the
    // camera's rotation matrix to get the correct world vector.
    dy = -dy;  // up is negative, but the calculations are easiest to
               // imagine up is positive.
    Eigen::Vector3f axis(float(-dy), float(dx), 0);  // rotate by 90 deg in 2D
    axis = axis.normalized();
    float theta = CalcRotateRadians(dx, dy);

    axis = matrix.rotation() * axis;  // convert axis to world coords
    rot_matrix = rot_matrix * Eigen::AngleAxisf(-theta, axis);

//    utility::LogInfo("dim {}, beofre", matrix.Dim);
//    utility::LogInfo("[ {} {} {} ]", matrix(0,0), matrix(0,1), matrix(0,2));
//    utility::LogInfo("[ {} {} {} ]", matrix(1,0), matrix(1,1), matrix(1,2));
//    utility::LogInfo("[ {} {} {} ]", matrix(2,0), matrix(2,1), matrix(2,2));

    auto pos = matrix * Eigen::Vector3f(0, 0, 0);
//    utility::LogInfo("after [ {} {} {} ]", pos(0,0), pos(1,0), pos(2,0));
    Eigen::Vector3f to_cor = center_of_rotation_ - pos;
    auto dist = to_cor.norm();
    // If the center of rotation is behind the camera we need to flip
    // the sign of 'dist'. We can just dotprod with the forward vector
    // of the camera. Forward is [0, 0, -1] for an identity matrix,
    // so forward is simply rotation * [0, 0, -1].
    Eigen::Vector3f forward =
            matrix.rotation() * Eigen::Vector3f{0.0f, 0.0f, -1.0f};
    if (to_cor.dot(forward) < 0) {
        dist = -dist;
    }
    Camera::Transform m;
    m.fromPositionOrientationScale(center_of_rotation_,
                                   rot_matrix * matrix.rotation(),
                                   Eigen::Vector3f(1, 1, 1));
    m.translate(Eigen::Vector3f(0, 0, dist));

    matrix_ = m;
}

void MatrixInteractorLogic::RotateWorld(int dx,
                                        int dy,
                                        const Eigen::Vector3f& x_axis,
                                        const Eigen::Vector3f& y_axis) {
    auto matrix = matrix_at_mouse_down_;  // copy

    dy = -dy;  // up is negative, but the calculations are easiest to
               // imagine up is positive.
    Eigen::Vector3f axis = dx * x_axis + dy * y_axis;
    axis = axis.normalized();
    float theta = CalcRotateRadians(dx, dy);

    axis = matrix.rotation() * axis;  // convert axis to world coords
    auto rot_matrix =
            Camera::Transform::Identity() * Eigen::AngleAxisf(-theta, axis);

    auto pos = matrix * Eigen::Vector3f(0, 0, 0);
    auto dist = (center_of_rotation_ - pos).norm();
    Camera::Transform m;
    m.fromPositionOrientationScale(center_of_rotation_,
                                   rot_matrix * matrix.rotation(),
                                   Eigen::Vector3f(1, 1, 1));
    m.translate(Eigen::Vector3f(0, 0, dist));

    matrix_ = m;
}

float MatrixInteractorLogic::CalcRotateRadians(int dx, int dy) {
    Eigen::Vector3f moved(float(dx), float(dy), 0);
    return 0.5f * float(M_PI) * moved.norm() / (0.5f * float(view_height_));
}
float MatrixInteractorLogic::CalcRotateRadians3D(int dx, int dy, int dz) {
    Eigen::Vector3f moved((float)dx, (float)dy, (float)dz);
    return 0.5f * float(M_PI) * moved.norm() / (0.5f * float(view_height_));
}

void MatrixInteractorLogic::Rotate3D(int dx, int dy, int dz) {
    auto matrix = matrix_at_mouse_down_;  // copy
    Eigen::AngleAxisf rot_matrix(0, Eigen::Vector3f(1, 0, 0));

    // We want to rotate as if we were rotating an imaginary trackball
    // centered at the point of rotation. To do this we need an axis
    // of rotation and an angle about the axis. To find the axis, we
    // imagine that the viewing plane has been translated into the screen
    // so that it intersects the center of rotation. The axis we want
    // to rotate around is perpendicular to the vector defined by (dx, dy)
    // (assuming +x is right and +y is up). (Imagine the situation if the
    // mouse movement is (100, 0) or (0, 100).) Now it is easy to find
    // the perpendicular in 2D. Conveniently, (axis.x, axis.y, 0) is the
    // correct axis in camera-local coordinates. We can multiply by the
    // camera's rotation matrix to get the correct world vector.
    dy = -dy;  // up is negative, but the calculations are easiest to
                                                     // imagine up is positive.
    Eigen::Vector3f axis(float(-dy), float(dx), float(dz));  // rotate by 90 deg in 2D
    axis = axis.normalized();

    float theta = CalcRotateRadians3D(dx, dy, dz);

    axis = matrix.rotation() * axis;  // convert axis to world coords
    rot_matrix = rot_matrix * Eigen::AngleAxisf(-theta, axis);

    //    utility::LogInfo("dim {}, beofre", matrix.Dim);
    //    utility::LogInfo("[ {} {} {} ]", matrix(0,0), matrix(0,1), matrix(0,2));
    //    utility::LogInfo("[ {} {} {} ]", matrix(1,0), matrix(1,1), matrix(1,2));
    //    utility::LogInfo("[ {} {} {} ]", matrix(2,0), matrix(2,1), matrix(2,2));

    auto pos = matrix * Eigen::Vector3f(0, 0, 0);
    //    utility::LogInfo("after [ {} {} {} ]", pos(0,0), pos(1,0), pos(2,0));
    Eigen::Vector3f to_cor = center_of_rotation_ - pos;
    auto dist = to_cor.norm();
    // If the center of rotation is behind the camera we need to flip
    // the sign of 'dist'. We can just dotprod with the forward vector
    // of the camera. Forward is [0, 0, -1] for an identity matrix,
    // so forward is simply rotation * [0, 0, -1].
    Eigen::Vector3f forward =
            matrix.rotation() * Eigen::Vector3f{0.0f, 0.0f, -1.0f};
    if (to_cor.dot(forward) < 0) {
        dist = -dist;
    }
    Camera::Transform m;
    m.fromPositionOrientationScale(center_of_rotation_,
                                   rot_matrix * matrix.rotation(),
                                   Eigen::Vector3f(1, 1, 1));
    m.translate(Eigen::Vector3f(0, 0, dist));

    matrix_ = m;
}
void MatrixInteractorLogic::Translate3D(int dx, int dy, int dz) {
}
void MatrixInteractorLogic::RotateZ(int dx, int dy) {
    // RotateZ rotates around the axis normal to the screen. Since we
    // will be rotating using camera coordinates, we want to rotate
    // about (0, 0, 1).
    Eigen::Vector3f axis(0, 0, 1);
    auto rad = CalcRotateZRadians(dx, dy);
    auto matrix = matrix_at_mouse_down_;  // copy
    matrix.rotate(Eigen::AngleAxisf(rad, axis));
    matrix_ = matrix;
}

void MatrixInteractorLogic::RotateZWorld(int dx,
                                         int dy,
                                         const Eigen::Vector3f& forward) {
    auto rad = CalcRotateZRadians(dx, dy);
    Eigen::AngleAxisf rot_matrix(rad, forward);

    Camera::Transform matrix = matrix_at_mouse_down_;  // copy
    matrix.translate(center_of_rotation_);
    matrix *= rot_matrix;
    matrix.translate(-center_of_rotation_);
    matrix_ = matrix;
}

float MatrixInteractorLogic::CalcRotateZRadians(int dx, int dy) {
    // Moving half the height should rotate 360 deg (= 2 * PI).
    // This makes it easy to rotate enough without rotating too much.
    return float(4.0 * M_PI * dy / view_height_);
}

void MatrixInteractorLogic::Dolly(float dy, DragType drag_type) {
    if (drag_type == DragType::MOUSE) {
        float dist = CalcDollyDist(dy, drag_type, matrix_at_mouse_down_);
        Dolly(dist, matrix_at_mouse_down_);  // copies the matrix
    } else {
        float dist = CalcDollyDist(dy, drag_type, matrix_);
        Dolly(dist, matrix_);
    }
}

// Note: we pass `matrix` by value because we want to copy it,
//       as translate() will be modifying it.
void MatrixInteractorLogic::Dolly(float z_dist, Camera::Transform matrix) {
    // Dolly is just moving the camera forward. Filament uses right as +x,
    // up as +y, and forward as -z (standard OpenGL coordinates). So to
    // move forward all we need to do is translate the camera matrix by
    // dist * (0, 0, -1). Note that translating by camera_->GetForwardVector
    // would be incorrect, since GetForwardVector() returns the forward
    // vector in world space, but the translation happens in camera space.)
    // Since we want trackpad down (negative) to go forward ("pulling" the
    // model toward the viewer) we need to negate dy.
    auto forward = Eigen::Vector3f(0, 0, -z_dist);  // zDist * (0, 0, -1)
    matrix.translate(forward);
    matrix_ = matrix;
}

float MatrixInteractorLogic::CalcDollyDist(float dy,
                                           DragType drag_type,
                                           const Camera::Transform& matrix) {
    float length =
            (center_of_rotation_ - matrix * Eigen::Vector3f(0.0f, 0.0f, 0.0f))
                    .norm();
    length = std::max(float(0.02 * model_size_), length);
    float dist = 0.0f;  // initialize to make GCC happy
    switch (drag_type) {
        case DragType::MOUSE:
            // Zoom out is "push away" or up, is a negative value for
            // mousing
            dist = float(dy) * 0.0025f * float(length);
            break;
        case DragType::TWO_FINGER:
            // Zoom out is "push away" or up, is a positive value for
            // two-finger scrolling, so we need to invert dy.
            dist = float(-dy) * 0.01f * float(length);
            break;
        case DragType::WHEEL:  // actual mouse wheel, same as two-fingers
            dist = float(-dy) * 0.05f * float(length);
            break;
        case DragType::SPACE_MOUSE:  // actual mouse wheel, same as two-fingers
            dist = float(-dy) * 0.008f * float(length);
            break;
    }
    return dist;
}

}  // namespace rendering
}  // namespace visualization
}  // namespace open3d
