#include "Math/Ray.h"

#include <algorithm>
#include <limits>

namespace {

// 单轴 slab 求交：更新 [tNear, tFar] 区间。
// 返回 false 表示射线与该轴 slab 无交集。
bool ClipAxis(float origin, float direction, float slabMin, float slabMax,
              float& tNear, float& tFar) {
    constexpr float kEpsilon = 1.0e-8f;
    if (direction > -kEpsilon && direction < kEpsilon) {
        // 方向与该轴平行：起点必须落在 slab 内，否则无交集。
        return origin >= slabMin && origin <= slabMax;
    }
    const float invDir = 1.0f / direction;
    float t0 = (slabMin - origin) * invDir;
    float t1 = (slabMax - origin) * invDir;
    if (t0 > t1) std::swap(t0, t1);
    tNear = std::max(tNear, t0);
    tFar = std::min(tFar, t1);
    return tNear <= tFar;
}

}  // namespace

// 射线与 AABB 相交（slab 算法）。
bool RayIntersectsAabb(const Ray& ray, const Aabb& box, float* tNear, float* tFar) {
    // 空盒不参与相交。
    if (box.min.x > box.max.x || box.min.y > box.max.y || box.min.z > box.max.z) {
        return false;
    }

    float nearT = -std::numeric_limits<float>::infinity();
    float farT = std::numeric_limits<float>::infinity();

    if (!ClipAxis(ray.origin.x, ray.direction.x, box.min.x, box.max.x, nearT, farT)) {
        return false;
    }
    if (!ClipAxis(ray.origin.y, ray.direction.y, box.min.y, box.max.y, nearT, farT)) {
        return false;
    }
    if (!ClipAxis(ray.origin.z, ray.direction.z, box.min.z, box.max.z, nearT, farT)) {
        return false;
    }

    if (tNear) *tNear = nearT;
    if (tFar) *tFar = farT;
    return true;
}
