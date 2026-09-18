#include "TextureImport.h"

#include "TextureCache.h"

#include <string>
#include <unordered_set>

// 1.3 中 baseColor 一律视为颜色纹理（sRGB）。
TextureSemantic TextureImport::SemanticForBaseColor() {
    return TextureSemantic::Color;
}

// 由资产里的 Sampler 索引生成采样器描述；无则用默认。
static SamplerDesc SamplerDescForTexture(const SceneAsset& asset, int samplerIndex) {
    SamplerDesc desc;
    if (samplerIndex >= 0 && samplerIndex < static_cast<int>(asset.samplers.size())) {
        const Sampler& sampler = asset.samplers[static_cast<size_t>(samplerIndex)];
        desc.wrapS = sampler.wrapS;
        desc.wrapT = sampler.wrapT;
        if (sampler.magFilter != 0) desc.magFilter = sampler.magFilter;
        if (sampler.minFilter != 0) desc.minFilter = sampler.minFilter;
    }
    return desc;
}

// 处理一个资产中所有被材质引用的图像。
TextureImportResult TextureImport::Import(const SceneAsset& asset,
                                          TextureCache& cache,
                                          Render& renderer) {
    TextureImportResult result;

    std::unordered_set<int> processedTextures;  // 避免同一纹理被多个材质重复上传

    for (const Material& material : asset.materials) {
        const int textureIndex = material.baseColorTexture;
        if (textureIndex < 0 || textureIndex >= static_cast<int>(asset.textures.size())) {
            continue;
        }

        ++result.stats.referenced;

        // 同一纹理只上传一次（跨材质复用）。
        if (processedTextures.count(textureIndex) != 0) {
            continue;
        }
        processedTextures.insert(textureIndex);

        const Texture& texture = asset.textures[static_cast<size_t>(textureIndex)];
        const int imageIndex = texture.image;
        if (imageIndex < 0 || imageIndex >= static_cast<int>(asset.images.size())) {
            ++result.stats.failed;
            result.stats.notes.push_back("纹理缺少有效图像索引");
            continue;
        }
        const Image& image = asset.images[static_cast<size_t>(imageIndex)];

        // 解码：外部路径优先，其次内嵌字节（内容哈希）。
        const DecodedImage* decoded = nullptr;
        bool cacheHit = false;
        std::string cacheKey;
        if (!image.resolvedPath.empty()) {
            decoded = cache.GetOrDecodeFile(image.resolvedPath, &cacheHit);
            cacheKey = "path:" + image.resolvedPath;
        } else if (!image.encodedData.empty()) {
            decoded = cache.GetOrDecodeBytes(image.encodedData.data(),
                                             image.encodedData.size(), &cacheHit);
            cacheKey = "hash:" + std::to_string(
                TextureCache::HashBytes(image.encodedData.data(), image.encodedData.size()));
        }

        if (!decoded || !decoded->valid()) {
            ++result.stats.failed;
            const std::string label = image.name.empty()
                ? std::to_string(imageIndex) : image.name;
            result.stats.notes.push_back("图像解码失败: " + label);
            continue;
        }
        if (cacheHit) ++result.stats.cacheHits;
        TextureDesc desc;
        desc.width = decoded->width;
        desc.height = decoded->height;
        desc.semantic = SemanticForBaseColor();
        desc.generateMipmaps = true;
        desc.sampler = SamplerDescForTexture(asset, texture.sampler);
        desc.pixels = decoded->pixels.data();
        desc.sizeBytes = decoded->pixels.size();
        desc.debugName = image.name;
        desc.cacheKey = cacheKey;

        const TextureHandle handle = renderer.CreateTexture(desc);
        if (handle == 0) {
            ++result.stats.failed;
            const std::string label = image.name.empty()
                ? std::to_string(imageIndex) : image.name;
            result.stats.notes.push_back("纹理上传失败: " + label);
            continue;
        }

        result.textureHandles.emplace(textureIndex, handle);
        ++result.stats.created;
        if (IsSrgbSemantic(desc.semantic)) ++result.stats.srgbCount;
        result.stats.totalMipLevels += static_cast<int>(ComputeMipLevelCount(
            decoded->width, decoded->height));
    }

    return result;
}

// 生成一行中文汇总文本。
std::string TextureImport::BuildSummary(const TextureImportStats& stats) {
    if (stats.referenced == 0) {
        return "纹理: 无材质引用图像";
    }
    std::string text = "纹理: 引用 " + std::to_string(stats.referenced) +
        " 张（新建 " + std::to_string(stats.created) +
        " / 缓存命中 " + std::to_string(stats.cacheHits) +
        " / 失败 " + std::to_string(stats.failed) + "），sRGB 颜色纹理 " +
        std::to_string(stats.srgbCount) + " 张，mip 合计 " +
        std::to_string(stats.totalMipLevels) + " 级";
    return text;
}
