#ifndef __ASSET_TANGENT_GENERATOR_H__
#define __ASSET_TANGENT_GENERATOR_H__

#include "Asset/MeshData.h"

// 顶点切线生成（1.5，法线贴图 TBN 基）。
//
// 用途：当资产未提供切线数据时，由位置 / 法线 / 纹理坐标 / 索引推导每顶点切线。
// 算法（逐三角形累加 + 逐顶点正交化，Lengyel 经典做法）：
//   1. 对每个三角形，由 ΔUV 与 ΔPos 解出该面在 U 方向的切线 T 与 V 方向的副切线 B；
//   2. 把 T、B 累加到三个顶点的切线与副切线累加器中；
//   3. 逐顶点用 Gram-Schmidt 对法线正交化 T（T' = normalize(T - N·(N·T))）；
//   4. 手性 w = (dot(cross(N, T'), B) < 0) ? -1 : +1，写入 tangent.w。
//
// 退化情形（零面积三角形、缺失 UV、法线为零）会回退到稳定默认值，保证结果有限且正交。
namespace TangentGenerator {

// 就地填充 mesh 中每个顶点的 tangent[4]。
// 会覆盖已有切线（调用方应先判断是否需要生成）。索引须指向有效顶点。
void GenerateTangents(MeshData& mesh);

}  // namespace TangentGenerator

#endif //__ASSET_TANGENT_GENERATOR_H__
