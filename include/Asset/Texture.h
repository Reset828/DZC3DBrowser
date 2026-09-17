#ifndef __ASSET_TEXTURE_H__
#define __ASSET_TEXTURE_H__

#include <cstdint>
#include <string>
#include <vector>

// 纹理资源描述。1.1 只定义结构，不加载像素。
struct Texture {
    std::string name;             // 源文件名或标识
    int width = 0;                // 像素宽
    int height = 0;               // 像素高
    int channels = 0;             // 通道数
    bool sRGB = true;             // 是否按 sRGB 采样
    bool generateMipmaps = true;  // 是否生成 mipmap
    std::vector<uint8_t> pixels;  // 预留：像素数据（1.1 为空）
};

#endif //__ASSET_TEXTURE_H__
