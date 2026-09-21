#include "Math/Aabb.h"

#include <algorithm>
#include <cmath>
#include <limits>

#include "Math/Transform.h"

// 返回空盒（min = +inf，max = -inf）。
Aabb AabbEmpty() {
    const float inf = std::numeric_limits<float>::infinity();
    Aabb box;
    box.min = { inf, inf, inf };
    box.max = { -inf, -inf, -inf };
    return box;
}

// 是否为空盒（任一轴 min > max）。
bool AabbIsEmpty(const Aabb& box) {
    return box.min.x > box.max.x || box.min.y > box.max.y || box.min.z > box.max.z;
}

// 是否有效：非空、所有分量有限、且各轴 min <= max。
bool AabbIsValid(const Aabb& box) {
    if (AabbIsEmpty(box)) return false;
    const float values[6] = { box.min.x, box.min.y, box.min.z,
                              box.max.x, box.max.y, box.max.z };
    for (const float value : values) {
        if (!std::isfinite(value)) return false;
    }
    return true;
}

// 中心点（空盒返回原点）。
Vec3 AabbCenter(const Aabb& box) {
    if (AabbIsEmpty(box)) return Vec3{ 0.0f, 0.0f, 0.0f };
    return Vec3{ (box.min.x + box.max.x) * 0.5f,
                 (box.min.y + box.max.y) * 0.5f,
                 (box.min.z + box.max.z) * 0.5f };
}

// 各轴尺寸 max - min（空盒返回 0）。
Vec3 AabbSize(const Aabb& box) {
    if (AabbIsEmpty(box)) return Vec3{ 0.0f, 0.0f, 0.0f };
    return Vec3{ box.max.x - box.min.x,
                 box.max.y - box.min.y,
                 box.max.z - box.min.z };
}

// 包围球半径：中心到 max 的距离（空盒返回 0）。
float AabbRadius(const Aabb& box) {
    if (AabbIsEmpty(box)) return 0.0f;
    const Vec3 half = AabbSize(box);
    const float hx = half.x * 0.5f;
    const float hy = half.y * 0.5f;
    const float hz = half.z * 0.5f;
    return std::sqrt(hx * hx + hy * hy + hz * hz);
}

// 用点扩展包围盒（对空盒扩展后即为该点）。
void AabbExpand(Aabb& box, const Vec3& point) {
    box.min.x = std::min(box.min.x, point.x);
    box.min.y = std::min(box.min.y, point.y);
    box.min.z = std::min(box.min.z, point.z);
    box.max.x = std::max(box.max.x, point.x);
    box.max.y = std::max(box.max.y, point.y);
    box.max.z = std::max(box.max.z, point.z);
}

// 两个包围盒的并集（任一为空则返回另一个）。
Aabb AabbUnion(const Aabb& a, const Aabb& b) {
    if (AabbIsEmpty(a)) return b;
    if (AabbIsEmpty(b)) return a;
    Aabb out;
    out.min = { std::min(a.min.x, b.min.x),
                std::min(a.min.y, b.min.y),
                std::min(a.min.z, b.min.z) };
    out.max = { std::max(a.max.x, b.max.x),
                std::max(a.max.y, b.max.y),
                std::max(a.max.z, b.max.z) };
    return out;
}

// 点是否在盒内（含边界；空盒恒 false）。
bool AabbContains(const Aabb& box, const Vec3& point) {
    if (AabbIsEmpty(box)) return false;
    return point.x >= box.min.x && point.x <= box.max.x &&
           point.y >= box.min.y && point.y <= box.max.y &&
           point.z >= box.min.z && point.z <= box.max.z;
}

// 用列主序矩阵变换包围盒（8 角点变换后重求并）。空盒返回空盒。
Aabb AabbTransform(const Aabb& box, const Mat4& matrix) {
    if (AabbIsEmpty(box)) return AabbEmpty();

    const Vec3 corners[8] = {
        { box.min.x, box.min.y, box.min.z }, { box.max.x, box.min.y, box.min.z },
        { box.min.x, box.max.y, box.min.z }, { box.max.x, box.max.y, box.min.z },
        { box.min.x, box.min.y, box.max.z }, { box.max.x, box.min.y, box.max.z },
        { box.min.x, box.max.y, box.max.z }, { box.max.x, box.max.y, box.max.z }
    };

    Aabb out = AabbEmpty();
    for (const Vec3& corner : corners) {
        AabbExpand(out, TransformPoint(matrix, corner));
    }
    return out;
}
