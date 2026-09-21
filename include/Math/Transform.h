#ifndef __TRANSFORM_H__
#define __TRANSFORM_H__

#include "Math/EngineTypes.h"

// 局部变换：平移 + 欧拉角旋转（度）+ 缩放。
//
// 约定（任务 2.1）：
//   - 坐标系：Z-up 右手系；+X 东、+Y 北、+Z 上。
//   - 单位：平移/缩放无量纲（沿用模型原始单位）；旋转为角度（度）。
//   - 旋转顺序：先绕 X、再绕 Y、最后绕 Z，即 R = Rz * Ry * Rx。
//   - 组合：局部矩阵 M = T * R * S（先缩放、再旋转、最后平移）。
//   - 矩阵存储：列主序（与 glm / GLSL 一致）。
struct Transform {
    Vec3 translation = { 0.0f, 0.0f, 0.0f };
    Vec3 rotationDegrees = { 0.0f, 0.0f, 0.0f };  // 欧拉角（度），顺序 X -> Y -> Z
    Vec3 scale = { 1.0f, 1.0f, 1.0f };

    // 返回局部矩阵 M = T * R * S（列主序）。
    Mat4 ToMatrix() const;
    // 是否为单位变换。
    bool IsIdentity() const;
};

// 返回单位矩阵（列主序）。
Mat4 TransformIdentityMatrix();
// 由 T / R（欧拉角度）/ S 构造矩阵（列主序）。
Mat4 TransformMatrixFromTrs(const Vec3& translation, const Vec3& rotationDegrees,
                            const Vec3& scale);
// 两个列主序矩阵相乘（result = a * b）。
Mat4 TransformMultiply(const Mat4& a, const Mat4& b);
// 用列主序矩阵变换点（含平移）。
Vec3 TransformPoint(const Mat4& matrix, const Vec3& point);
// 用列主序矩阵变换方向向量（忽略平移）。
Vec3 TransformVector(const Mat4& matrix, const Vec3& vector);
// 求列主序矩阵的逆（任务 2.3）。成功返回 true 并写入 out；矩阵奇异时返回 false。
// 用于把世界射线变换到对象局部空间（射线拾取）。
bool TransformInvert(const Mat4& matrix, Mat4& out);
// 从矩阵分解出 T / R / S（旋转用欧拉角度，顺序 X -> Y -> Z）。
Transform TransformFromMatrix(const Mat4& matrix);

#endif //__TRANSFORM_H__
