#ifndef __AABB_H__
#define __AABB_H__

#include "Math/EngineTypes.h"

// AABB 工具（任务 2.2）。
//
// 约定：
//  - 坐标系与 Transform 一致：Z-up 右手系；Aabb 用 min / max 两个角点表示。
//  - “空盒”（无几何）：min 全为 +inf、max 全为 -inf（见 AabbEmpty）。
//    空盒与任意盒求并 = 另一个盒；空盒不参与包围盒累加。
//  - “退化盒”：min == max（一个点，例如单顶点网格）。
//  - “无效盒”：出现 NaN，或某轴 min > max。空盒不算“无效”之外的东西，需单独用
//    AabbIsEmpty 判断（AabbIsValid 对空盒返回 false）。
//  - 变换：AabbTransform 对 8 个角点逐一变换后重新求并，因此旋转 / 非均匀缩放后仍正确
//    （若只变换 min / max 两角，旋转会得到错误的盒）。

// 返回空盒（min = +inf，max = -inf）。
Aabb AabbEmpty();
// 是否为空盒（任一轴 min > max）。
bool AabbIsEmpty(const Aabb& box);
// 是否有效：非空、所有分量有限、且各轴 min <= max。
bool AabbIsValid(const Aabb& box);
// 中心点（空盒返回原点）。
Vec3 AabbCenter(const Aabb& box);
// 各轴尺寸 max - min（空盒返回 0）。
Vec3 AabbSize(const Aabb& box);
// 包围球半径：中心到 max 的距离（空盒返回 0）。
float AabbRadius(const Aabb& box);
// 用点扩展包围盒（对空盒扩展后即为该点）。
void AabbExpand(Aabb& box, const Vec3& point);
// 两个包围盒的并集（任一为空则返回另一个）。
Aabb AabbUnion(const Aabb& a, const Aabb& b);
// 点是否在盒内（含边界；空盒恒 false）。
bool AabbContains(const Aabb& box, const Vec3& point);
// 用列主序矩阵变换包围盒（8 角点变换后重求并）。空盒返回空盒。
Aabb AabbTransform(const Aabb& box, const Mat4& matrix);

#endif //__AABB_H__
