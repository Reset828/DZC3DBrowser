#pragma once

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

// 解码后的 CPU 图像（RGBA8，紧密排列，宽*高*4 字节）。
// 常驻缓存，跨模型与后端切换存活；GPU 纹理由渲染器从它重建。
struct DecodedImage {
    uint32_t width = 0;
    uint32_t height = 0;
    std::vector<uint8_t> pixels;
    bool valid() const { return width > 0 && height > 0 && !pixels.empty(); }
};

// 纹理 CPU 缓存：按“文件路径”或“内容哈希”为键，避免同一图像重复解码/上传。
// 由 MainWindow 拥有，生命周期跨越模型增删与后端切换。
class TextureCache {
public:
    // 由外部路径获取/解码（键 = 规范化绝对路径）。失败返回 nullptr。
    // cacheHit：若命中的是已有缓存项则置 true（未重复解码）。
    const DecodedImage* GetOrDecodeFile(const std::string& path, bool* cacheHit = nullptr);
    // 由内嵌编码字节获取/解码（键 = 内容 FNV-1a 哈希）。失败返回 nullptr。
    const DecodedImage* GetOrDecodeBytes(const uint8_t* data, size_t size,
                                         bool* cacheHit = nullptr);
    // 计算内容哈希（供调用方作为稳定键）。
    static uint64_t HashBytes(const uint8_t* data, size_t size);

    // 统计（供消息区展示）。
    size_t Count() const { return m_images.size(); }

private:
    // 用 QImage 解码 PNG/JPEG 等并转成 RGBA8。
    static bool DecodeToRgba8(const uint8_t* data, size_t size, DecodedImage& out);

    std::unordered_map<std::string, DecodedImage> m_images;  // 键：path: 或 hash:
};
