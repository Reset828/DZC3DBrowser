#ifndef __ASSET_SUBMESH_H__
#define __ASSET_SUBMESH_H__

#include <cstdint>
#include "Math/EngineTypes.h"

// 网格中的一段索引区间及其材质。
struct SubMesh {
    uint32_t indexOffset = 0;  // 在 MeshData::indices 中的起始索引
    uint32_t indexCount = 0;   // 索引数量
    int materialIndex = -1;    // 指向 SceneAsset::materials；-1 表示无
    Aabb localBounds;          // 局部包围盒
};

#endif //__ASSET_SUBMESH_H__
