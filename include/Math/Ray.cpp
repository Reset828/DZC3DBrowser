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

// 射线与三角形相交（Möller–Trumbore，任务 2.3，CPU 精确拾取用）。
bool RayIntersectsTriangle(const Ray& ray, const Vec3& a, const Vec3& b, const Vec3& c,
                           float* t, float* u, float* v) {
    constexpr float kEpsilon = 1.0e-8f;

    const Vec3 edge1{ b.x - a.x, b.y - a.y, b.z - a.z };
    const Vec3 edge2{ c.x - a.x, c.y - a.y, c.z - a.z };

    const Vec3 pvec{
        ray.direction.y * edge2.z - ray.direction.z * edge2.y,
        ray.direction.z * edge2.x - ray.direction.x * edge2.z,
        ray.direction.x * edge2.y - ray.direction.y * edge2.x
    };
    const float det = edge1.x * pvec.x + edge1.y * pvec.y + edge1.z * pvec.z;
    // 行列式接近 0：射线与三角形平面平行（或退化三角形）。
    if (det > -kEpsilon && det < kEpsilon) return false;

    const float invDet = 1.0f / det;
    const Vec3 tvec{ ray.origin.x - a.x, ray.origin.y - a.y, ray.origin.z - a.z };

    const float baryU = (tvec.x * pvec.x + tvec.y * pvec.y + tvec.z * pvec.z) * invDet;
    if (baryU < 0.0f || baryU > 1.0f) return false;

    const Vec3 qvec{
        tvec.y * edge1.z - tvec.z * edge1.y,
        tvec.z * edge1.x - tvec.x * edge1.z,
        tvec.x * edge1.y - tvec.y * edge1.x
    };
    const float baryV =
        (ray.direction.x * qvec.x + ray.direction.y * qvec.y + ray.direction.z * qvec.z) * invDet;
    if (baryV < 0.0f || baryU + baryV > 1.0f) return false;

    const float hitT =
        (edge2.x * qvec.x + edge2.y * qvec.y + edge2.z * qvec.z) * invDet;
    if (hitT <= kEpsilon) return false;

    if (t) *t = hitT;
    if (u) *u = baryU;
    if (v) *v = baryV;
    return true;
}
