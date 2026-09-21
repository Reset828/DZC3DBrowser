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
    Aabb bounds;  // 整个网格的包围盒（节点局部空间）
    // 任务 2.1：产生该网格的节点索引（指向 SceneAsset::nodes）；-1 表示无（如 OBJ）。
    // 顶点保留在节点局部空间，节点变换由运行时场景图施加。
    int nodeIndex = -1;
};

#endif //__ASSET_MESH_H__
