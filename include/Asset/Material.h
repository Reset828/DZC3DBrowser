#ifndef __ASSET_MATERIAL_H__
#define __ASSET_MATERIAL_H__

#include <string>
#include "Math/EngineTypes.h"

// 材质描述。1.1 只含名称、基础色与纹理引用；暂不参与着色。
struct Material {
    std::string name;
    Vec4 baseColor = { 1.0f, 1.0f, 1.0f, 1.0f };
    int baseColorTexture = -1;  // 指向 SceneAsset::textures；-1 表示无
};

#endif //__ASSET_MATERIAL_H__
