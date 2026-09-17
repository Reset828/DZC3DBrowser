#ifndef __ASSET_TEXTURE_H__
#define __ASSET_TEXTURE_H__

#include <string>

// 纹理 = 图像 + 采样器的引用组合（对齐 glTF 的 texture）。
struct Texture {
    std::string name;
    int image = -1;    // 指向 SceneAsset::images；-1 表示无
    int sampler = -1;  // 指向 SceneAsset::samplers；-1 表示无
};

#endif //__ASSET_TEXTURE_H__
