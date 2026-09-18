#ifndef __ASSET_MATERIAL_H__
#define __ASSET_MATERIAL_H__

#include <string>
#include "Math/EngineTypes.h"

// 材质透明度分类（对齐 glTF alphaMode）。
enum class MaterialAlphaMode {
    Opaque = 0,  // 完全不透明
    Mask = 1,    // 按 alphaCutoff 做 alpha 测试（片元 discard）
    Blend = 2    // alpha 混合，需要按深度排序后绘制
};

// 材质描述。1.4：名称、基础色、基础色纹理与透明度分类。
// 1.5：Metallic-Roughness PBR 参数（metallic/roughness/法线/AO/自发光）。
//
// 参数范围与默认值（对齐 glTF 2.0 pbrMetallicRoughness 语义）：
//   metallic         [0,1]，0 = 电介质（非金属），1 = 金属；默认 0（结构体安全默认）。
//   roughness        [0,1]，0 = 镜面，1 = 完全粗糙；默认 0.5。
//   normalScale      法线强度缩放；默认 1。glTF 解析时按 normalTexture.scale 覆盖。
//   occlusionStrength [0,1]，AO 强度；默认 1。glTF 解析时按 occlusionTexture.strength 覆盖。
//   emissiveFactor   [0,1]^3 线性自发光色；默认黑（无自发光）。
//   emissiveStrength 自发光倍率（KHR_materials_emissive_strength 的简化）；默认 1。
// 注意：glTF 规范默认 metallicFactor/roughnessFactor 均为 1，由解析器显式写入。
struct Material {
    std::string name;
    Vec4 baseColor = { 1.0f, 1.0f, 1.0f, 1.0f };
    int baseColorTexture = -1;  // 指向 SceneAsset::textures；-1 表示无
    MaterialAlphaMode alphaMode = MaterialAlphaMode::Opaque;
    float alphaCutoff = 0.5f;   // Mask 模式的 alpha 阈值

    // --- Metallic-Roughness PBR（1.5） ---
    float metallic = 0.0f;
    float roughness = 0.5f;
    // metallic-roughness 合并图（glTF 约定：G=粗糙度，B=金属度）；-1 表示无。
    int metallicRoughnessTexture = -1;
    int normalTexture = -1;          // 切线空间法线贴图；-1 表示无
    float normalScale = 1.0f;        // 法线强度
    int occlusionTexture = -1;       // AO 贴图（R 通道）；-1 表示无
    float occlusionStrength = 1.0f;  // AO 强度
    Vec3 emissiveFactor = { 0.0f, 0.0f, 0.0f };  // 线性自发光色
    int emissiveTexture = -1;        // 自发光贴图；-1 表示无
    float emissiveStrength = 1.0f;   // 自发光倍率
};

#endif //__ASSET_MATERIAL_H__
