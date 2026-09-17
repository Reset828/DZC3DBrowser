#ifndef __ASSET_MESH_H__
#define __ASSET_MESH_H__

#include <cstdint>
#include <string>
#include <vector>
#include "Asset/AssetVertex.h"
#include "Asset/SubMesh.h"
#include "Math/EngineTypes.h"

// 一个网格：顶点、索引、包围盒与 SubMesh 划分。
struct MeshData {
    std::string name;
    std::vector<AssetVertex> vertices;
    std::vector<uint32_t> indices;
    std::vector<SubMesh> subMeshes;
    Aabb bounds;  // 整个网格的包围盒
};

#endif //__ASSET_MESH_H__
