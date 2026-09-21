#include "Math/Transform.h"

#include <algorithm>
#include <cmath>

namespace {

// 角度转弧度。
float ToRadians(float degrees) {
    return degrees * 3.14159265358979323846f / 180.0f;
}

// 由行主序元素写入列主序矩阵（m[列][行]）。
Mat4 MakeColumnMajor(float r00, float r01, float r02,
                     float r10, float r11, float r12,
                     float r20, float r21, float r22) {
    Mat4 out{};
    out.m[0][0] = r00; out.m[0][1] = r10; out.m[0][2] = r20;
    out.m[1][0] = r01; out.m[1][1] = r11; out.m[1][2] = r21;
    out.m[2][0] = r02; out.m[2][1] = r12; out.m[2][2] = r22;
    out.m[3][3] = 1.0f;
    return out;
}

}  // namespace

// 返回单位矩阵（列主序）。
Mat4 TransformIdentityMatrix() {
    Mat4 out{};
    out.m[0][0] = 1.0f;
    out.m[1][1] = 1.0f;
    out.m[2][2] = 1.0f;
    out.m[3][3] = 1.0f;
    return out;
}

// 两个列主序矩阵相乘（result = a * b）。
Mat4 TransformMultiply(const Mat4& a, const Mat4& b) {
    Mat4 out{};
    for (int col = 0; col < 4; ++col) {
        for (int row = 0; row < 4; ++row) {
            float sum = 0.0f;
            for (int k = 0; k < 4; ++k) {
                sum += a.m[k][row] * b.m[col][k];
            }
            out.m[col][row] = sum;
        }
    }
    return out;
}

// 由 T / R（欧拉角度，顺序 X -> Y -> Z）/ S 构造矩阵（列主序）。
// M = T * Rz * Ry * Rx * S。
Mat4 TransformMatrixFromTrs(const Vec3& translation, const Vec3& rotationDegrees,
                            const Vec3& scale) {
    const float rx = ToRadians(rotationDegrees.x);
    const float ry = ToRadians(rotationDegrees.y);
    const float rz = ToRadians(rotationDegrees.z);
    const float cx = std::cos(rx), sx = std::sin(rx);
    const float cy = std::cos(ry), sy = std::sin(ry);
    const float cz = std::cos(rz), sz = std::sin(rz);

    // R = Rz * Ry * Rx（行主序）。
    const float r00 = cy * cz;
    const float r01 = cz * sx * sy - cx * sz;
    const float r02 = cx * cz * sy + sx * sz;
    const float r10 = cy * sz;
    const float r11 = cx * cz + sx * sy * sz;
    const float r12 = -cz * sx + cx * sy * sz;
    const float r20 = -sy;
    const float r21 = cy * sx;
    const float r22 = cx * cy;

    // R * S：缩放各列（行主序下缩放作用在列上）。
    const float s00 = r00 * scale.x, s01 = r01 * scale.y, s02 = r02 * scale.z;
    const float s10 = r10 * scale.x, s11 = r11 * scale.y, s12 = r12 * scale.z;
    const float s20 = r20 * scale.x, s21 = r21 * scale.y, s22 = r22 * scale.z;

    Mat4 out = MakeColumnMajor(s00, s01, s02,
                               s10, s11, s12,
                               s20, s21, s22);
    // 平移写入第 4 列。
    out.m[3][0] = translation.x;
    out.m[3][1] = translation.y;
    out.m[3][2] = translation.z;
    out.m[3][3] = 1.0f;
    return out;
}

// 返回局部矩阵 M = T * R * S（列主序）。
Mat4 Transform::ToMatrix() const {
    return TransformMatrixFromTrs(translation, rotationDegrees, scale);
}

// 是否为单位变换。
bool Transform::IsIdentity() const {
    const float eps = 1.0e-6f;
    return std::abs(translation.x) < eps && std::abs(translation.y) < eps &&
           std::abs(translation.z) < eps &&
           std::abs(rotationDegrees.x) < eps && std::abs(rotationDegrees.y) < eps &&
           std::abs(rotationDegrees.z) < eps &&
           std::abs(scale.x - 1.0f) < eps && std::abs(scale.y - 1.0f) < eps &&
           std::abs(scale.z - 1.0f) < eps;
}

// 用列主序矩阵变换点（含平移）。
Vec3 TransformPoint(const Mat4& matrix, const Vec3& point) {
    Vec3 out{};
    out.x = matrix.m[0][0] * point.x + matrix.m[1][0] * point.y +
            matrix.m[2][0] * point.z + matrix.m[3][0];
    out.y = matrix.m[0][1] * point.x + matrix.m[1][1] * point.y +
            matrix.m[2][1] * point.z + matrix.m[3][1];
    out.z = matrix.m[0][2] * point.x + matrix.m[1][2] * point.y +
            matrix.m[2][2] * point.z + matrix.m[3][2];
    return out;
}

// 用列主序矩阵变换方向向量（忽略平移）。
Vec3 TransformVector(const Mat4& matrix, const Vec3& vector) {
    Vec3 out{};
    out.x = matrix.m[0][0] * vector.x + matrix.m[1][0] * vector.y + matrix.m[2][0] * vector.z;
    out.y = matrix.m[0][1] * vector.x + matrix.m[1][1] * vector.y + matrix.m[2][1] * vector.z;
    out.z = matrix.m[0][2] * vector.x + matrix.m[1][2] * vector.y + matrix.m[2][2] * vector.z;
    return out;
}

// 从矩阵分解出 T / R / S（旋转用欧拉角度，顺序 X -> Y -> Z）。
Transform TransformFromMatrix(const Mat4& matrix) {
    Transform out;
    out.translation = { matrix.m[3][0], matrix.m[3][1], matrix.m[3][2] };

    // 列向量长度 = 缩放（负缩放会被折进旋转，这里按正缩放处理）。
    auto columnLength = [&matrix](int col) {
        return std::sqrt(matrix.m[col][0] * matrix.m[col][0] +
                         matrix.m[col][1] * matrix.m[col][1] +
                         matrix.m[col][2] * matrix.m[col][2]);
    };
    out.scale.x = columnLength(0);
    out.scale.y = columnLength(1);
    out.scale.z = columnLength(2);

    // 归一化得到纯旋转（列主序 m[列][行] -> 行主序 r[row][col]）。
    auto safeDivide = [](float value, float divisor) {
        return divisor > 1.0e-8f ? value / divisor : 0.0f;
    };
    const float r00 = safeDivide(matrix.m[0][0], out.scale.x);
    const float r10 = safeDivide(matrix.m[0][1], out.scale.x);
    const float r20 = safeDivide(matrix.m[0][2], out.scale.x);
    const float r01 = safeDivide(matrix.m[1][0], out.scale.y);
    const float r11 = safeDivide(matrix.m[1][1], out.scale.y);
    const float r21 = safeDivide(matrix.m[1][2], out.scale.y);
    const float r02 = safeDivide(matrix.m[2][0], out.scale.z);
    const float r12 = safeDivide(matrix.m[2][1], out.scale.z);
    const float r22 = safeDivide(matrix.m[2][2], out.scale.z);

    // 由 R = Rz * Ry * Rx 反解欧拉角（度）。
    const float sy = std::clamp(-r20, -1.0f, 1.0f);
    const float ry = std::asin(sy);
    float rx = 0.0f;
    float rz = 0.0f;
    if (std::abs(r20) < 0.999999f) {
        rx = std::atan2(r21, r22);
        rz = std::atan2(r10, r00);
    } else {
        // 万向锁：X 与 Z 退化，约定 Z = 0，仅解 X。
        rx = std::atan2(-r12, r11);
        rz = 0.0f;
    }
    const float kRadToDeg = 180.0f / 3.14159265358979323846f;
    out.rotationDegrees = { rx * kRadToDeg, ry * kRadToDeg, rz * kRadToDeg };
    return out;
}
