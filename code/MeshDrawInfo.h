#pragma once

#include <cstdint>
#include <unordered_map>
#include <vector>

#include "Asset/Material.h"
#include "Asset/MeshData.h"
#include "Render/Render.h"  // MaterialParams / TextureHandle

// 一个 SubMesh 的绘制信息：索引区间 + 解析后的材质参数 + 纹理句柄集合 + 可见性。
// 上传后保留在 Mesh 对象上，供逐 SubMesh 绘制与透明排序使用。
struct SubMeshDrawInfo {
    uint32_t indexOffset = 0;   // 在索引缓冲中的起始索引
    uint32_t indexCount = 0;    // 索引数量
    int materialIndex = -1;     // 指向资产 materials；-1 表示无材质
    MaterialParams material;    // 材质参数（baseColor/metallic/roughness/alphaMode/...）
    MaterialTextureSet textures;  // 全部 PBR 贴图句柄（0 = 使用默认贴图）
    bool visible = true;        // 材质可见性（材质面板可切换）
    float center[3] = { 0.0f, 0.0f, 0.0f };  // 该 SubMesh 中心（上传坐标空间），用于透明排序
    float radius = 0.0f;        // 该 SubMesh 包围球半径，用于排序兜底
};

// 由（已归一化的）MeshData、材质表与纹理句柄表构建逐 SubMesh 绘制信息。
// textureHandles 的键为 SceneAsset::textures 索引。
std::vector<SubMeshDrawInfo> BuildSubMeshDrawInfos(
    const MeshData& mesh,
    const std::vector<Material>& materials,
    const std::unordered_map<int, TextureHandle>& textureHandles);
