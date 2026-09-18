#include "MeshDrawInfo.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace {

// 取材质基础色/PBR/透明度分类到材质参数块（1.5）。
MaterialParams MakeMaterialParams(const Material* material) {
    MaterialParams params;
    if (material) {
        params.baseColor[0] = material->baseColor.x;
        params.baseColor[1] = material->baseColor.y;
        params.baseColor[2] = material->baseColor.z;
        params.baseColor[3] = material->baseColor.w;
        params.emissiveFactor[0] = material->emissiveFactor.x;
        params.emissiveFactor[1] = material->emissiveFactor.y;
        params.emissiveFactor[2] = material->emissiveFactor.z;
        params.emissiveFactor[3] = 0.0f;
        params.alphaCutoff = material->alphaCutoff;
        params.metallic = material->metallic;
        params.roughness = material->roughness;
        params.normalScale = material->normalScale;
        params.occlusionStrength = material->occlusionStrength;
        params.emissiveStrength = material->emissiveStrength;
        params.alphaMode = static_cast<int>(material->alphaMode);
    }
    return params;
}

// 解析材质引用的各 PBR 纹理句柄（无则 0，交给渲染器替换默认贴图）。
MaterialTextureSet ResolveTextures(const Material* material,
                                   const std::unordered_map<int, TextureHandle>& handles) {
    MaterialTextureSet set;
    if (!material) return set;
    auto resolve = [&handles](int textureIndex) -> TextureHandle {
        if (textureIndex < 0) return 0;
        const auto found = handles.find(textureIndex);
        return found == handles.end() ? 0 : found->second;
    };
    set.baseColor = resolve(material->baseColorTexture);
    set.metallicRoughness = resolve(material->metallicRoughnessTexture);
    set.normal = resolve(material->normalTexture);
    set.occlusion = resolve(material->occlusionTexture);
    set.emissive = resolve(material->emissiveTexture);
    return set;
}

// 计算一段索引区间的中心与半径（上传坐标空间）。
void ComputeRangeBounds(const MeshData& mesh, uint32_t offset, uint32_t count,
                        float center[3], float& radius) {
    float minX = std::numeric_limits<float>::max();
    float minY = std::numeric_limits<float>::max();
    float minZ = std::numeric_limits<float>::max();
    float maxX = std::numeric_limits<float>::lowest();
    float maxY = std::numeric_limits<float>::lowest();
    float maxZ = std::numeric_limits<float>::lowest();

    const size_t end = static_cast<size_t>(offset) + count;
    for (size_t i = offset; i < end && i < mesh.indices.size(); ++i) {
        const uint32_t vertexIndex = mesh.indices[i];
        if (vertexIndex >= mesh.vertices.size()) continue;
        const AssetVertex& vertex = mesh.vertices[vertexIndex];
        minX = std::min(minX, vertex.position[0]);
        minY = std::min(minY, vertex.position[1]);
        minZ = std::min(minZ, vertex.position[2]);
        maxX = std::max(maxX, vertex.position[0]);
        maxY = std::max(maxY, vertex.position[1]);
        maxZ = std::max(maxZ, vertex.position[2]);
    }

    if (minX > maxX) {
        center[0] = center[1] = center[2] = 0.0f;
        radius = 0.0f;
        return;
    }
    center[0] = (minX + maxX) * 0.5f;
    center[1] = (minY + maxY) * 0.5f;
    center[2] = (minZ + maxZ) * 0.5f;
    const float dx = (maxX - minX) * 0.5f;
    const float dy = (maxY - minY) * 0.5f;
    const float dz = (maxZ - minZ) * 0.5f;
    radius = std::sqrt(dx * dx + dy * dy + dz * dz);
}

}  // namespace

// 由资产网格、材质表与纹理句柄表构建逐 SubMesh 绘制信息。
std::vector<SubMeshDrawInfo> BuildSubMeshDrawInfos(
    const MeshData& mesh,
    const std::vector<Material>& materials,
    const std::unordered_map<int, TextureHandle>& textureHandles) {
    std::vector<SubMeshDrawInfo> infos;

    auto makeInfo = [&](uint32_t offset, uint32_t count, int materialIndex) {
        SubMeshDrawInfo info;
        info.indexOffset = offset;
        info.indexCount = count;
        info.materialIndex = materialIndex;
        const Material* material =
            (materialIndex >= 0 && materialIndex < static_cast<int>(materials.size()))
                ? &materials[static_cast<size_t>(materialIndex)]
                : nullptr;
        info.material = MakeMaterialParams(material);
        info.textures = ResolveTextures(material, textureHandles);
        info.visible = true;
        ComputeRangeBounds(mesh, offset, count, info.center, info.radius);
        infos.push_back(info);
    };

    if (mesh.subMeshes.empty()) {
        // 没有 SubMesh 划分时，整段索引视为一个默认材质 SubMesh。
        if (!mesh.indices.empty()) {
            makeInfo(0, static_cast<uint32_t>(mesh.indices.size()), -1);
        }
        return infos;
    }

    for (const SubMesh& subMesh : mesh.subMeshes) {
        if (subMesh.indexCount == 0) continue;
        makeInfo(subMesh.indexOffset, subMesh.indexCount, subMesh.materialIndex);
    }
    return infos;
}
