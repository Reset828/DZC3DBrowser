#ifndef __RAY_H__
#define __RAY_H__

#include "Math/EngineTypes.h"

// 射线（任务 2.2）：origin + 单位化或非单位化的方向。
// 约定：t 以 direction 的长度为单位；若 direction 已单位化，则 t 即世界距离。
// 用于 Ray-AABB 相交，为后续 Picking / 视锥剔除打基础（屏幕反投影留待 2.3）。
struct Ray {
    Vec3 origin = { 0.0f, 0.0f, 0.0f };
    Vec3 direction = { 0.0f, 0.0f, -1.0f };
};

// 射线与 AABB 相交（slab 算法）。
// 返回是否命中；命中时通过 tNear / tFar 输出进入 / 离开距离（可为负，表示射线起点
// 在盒内或盒在射线反向延长线上，调用方按需裁剪）。tNear / tFar 可为 nullptr。
// 空盒或退化盒（某轴 min == max）按普通盒处理，不特殊报错。
// 方向分量为 0 的轴：若射线起点在该轴 slab 之外则不相交，否则该轴不限制 t。
bool RayIntersectsAabb(const Ray& ray, const Aabb& box, float* tNear = nullptr,
                       float* tFar = nullptr);

#endif //__RAY_H__
