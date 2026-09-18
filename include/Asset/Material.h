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
// PBR 参数（metallic/roughness/normal/...）留到 1.5。
struct Material {
    std::string name;
    Vec4 baseColor = { 1.0f, 1.0f, 1.0f, 1.0f };
    int baseColorTexture = -1;  // 指向 SceneAsset::textures；-1 表示无
    MaterialAlphaMode alphaMode = MaterialAlphaMode::Opaque;
    float alphaCutoff = 0.5f;   // Mask 模式的 alpha 阈值
};

#endif //__ASSET_MATERIAL_H__
