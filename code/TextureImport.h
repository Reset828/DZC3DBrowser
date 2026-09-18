#pragma once

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

#include "Asset/SceneAsset.h"
#include "Render/Render.h"

class TextureCache;

// 一次纹理导入的统计（供消息区展示）。
struct TextureImportStats {
    int referenced = 0;   // 被材质引用的图像数量
    int created = 0;      // 新建并上传的 GPU 纹理数量
    int cacheHits = 0;    // 命中 CPU 缓存、未重复解码的数量
    int failed = 0;       // 解码或上传失败的图像数量
    int srgbCount = 0;    // 使用 sRGB 格式的颜色纹理数量
    int totalMipLevels = 0;  // 全部纹理的 mip 级数之和
    std::vector<std::string> notes;  // 失败/警告细节
};

// 纹理导入结果：每个 (材质索引) 对应的 GPU 纹理句柄，以及本次统计。
// handle 0 表示该材质没有可用纹理。
struct TextureImportResult {
    // 键为 SceneAsset::textures 的索引；值为 GPU 纹理句柄。
    std::unordered_map<int, TextureHandle> textureHandles;
    TextureImportStats stats;
};

// 纹理资源导入器（1.3）。
// 只处理“被材质引用的图像”：解码（经 TextureCache）-> 创建 GPU 纹理（经 Render）。
// 不参与着色采样；采样与材质链路属于 1.4。
class TextureImport {
public:
    // 处理一个资产中所有被材质引用的图像。
    // cache：常驻 CPU 缓存（外部拥有）。
    // renderer：当前渲染器（外部拥有），需已初始化。
    static TextureImportResult Import(const SceneAsset& asset,
                                      TextureCache& cache,
                                      Render& renderer);

    // 生成一行中文汇总文本（供消息区）。
    static std::string BuildSummary(const TextureImportStats& stats);

private:
    // 依据纹理语义推断颜色空间（1.3 里 baseColor 一律视为颜色纹理）。
    static TextureSemantic SemanticForBaseColor();
};
