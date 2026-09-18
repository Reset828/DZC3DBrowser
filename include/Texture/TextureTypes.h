#ifndef __TEXTURE_TYPES_H__
#define __TEXTURE_TYPES_H__

#include <cstddef>
#include <cstdint>
#include <string>

// 纹理资源系统的后端无关描述（1.3）。
// 本头文件不包含 Qt / Vulkan / OpenGL 头，供 Render 基类与两个后端共用。

// 纹理语义：决定颜色空间（sRGB 还是线性）与用途分类。
//   Color / Emissive        -> sRGB 颜色纹理
//   Normal / Roughness /
//   Metallic / Occlusion /
//   Generic                 -> 线性数据纹理
enum class TextureSemantic {
    Color,
    Emissive,
    Normal,
    Roughness,
    Metallic,
    Occlusion,
    Generic
};

// 该语义是否应使用 sRGB 存储格式（颜色纹理）。
inline bool IsSrgbSemantic(TextureSemantic semantic) {
    return semantic == TextureSemantic::Color
        || semantic == TextureSemantic::Emissive;
}

// 采样器描述。字段沿用 glTF / OpenGL 枚举值（1.2 的 Sampler 已用同一约定）。
struct SamplerDesc {
    int wrapS = 10497;      // GL_REPEAT
    int wrapT = 10497;      // GL_REPEAT
    int minFilter = 9987;   // GL_LINEAR_MIPMAP_LINEAR
    int magFilter = 9729;   // GL_LINEAR
};

// 创建一张 GPU 纹理所需的全部输入。pixels 为 RGBA8、紧密排列（宽 * 高 * 4）。
struct TextureDesc {
    uint32_t width = 0;
    uint32_t height = 0;
    TextureSemantic semantic = TextureSemantic::Color;
    bool generateMipmaps = true;
    SamplerDesc sampler;
    const uint8_t* pixels = nullptr;
    size_t sizeBytes = 0;
    std::string debugName;
    // 跨模型复用键（如 "path:..."/"hash:..."）。非空且已存在时，渲染器复用同一 GPU
    // 纹理并增加引用计数，避免同一图像重复上传；空则每次创建独立纹理。
    std::string cacheKey;
};

// 后端返回的纹理句柄；0 表示无效。
using TextureHandle = uint32_t;

// 计算完整 mip 链级数（含 base level）。
inline uint32_t ComputeMipLevelCount(uint32_t width, uint32_t height) {
    uint32_t levels = 1;
    uint32_t size = width > height ? width : height;
    while (size > 1) {
        size >>= 1;
        ++levels;
    }
    return levels;
}

#endif //__TEXTURE_TYPES_H__
