#include "TextureImport.h"

#include "TextureCache.h"

#include <string>
#include <unordered_map>

// 1.5：按用途映射颜色空间。颜色/自发光为 sRGB，其余（法线/粗糙度/金属度/AO）为线性。
TextureSemantic TextureImport::SemanticForUsage(int usage) {
    switch (static_cast<MaterialTextureUsage>(usage)) {
    case MaterialTextureUsage::BaseColor:
        return TextureSemantic::Color;
    case MaterialTextureUsage::Emissive:
        return TextureSemantic::Emissive;
    case MaterialTextureUsage::Normal:
        return TextureSemantic::Normal;
    case MaterialTextureUsage::Occlusion:
        return TextureSemantic::Occlusion;
    case MaterialTextureUsage::MetallicRoughness:
        // metallic-roughness 合并图属于数据贴图（线性）；G=roughness、B=metallic。
        return TextureSemantic::Roughness;
    }
    return TextureSemantic::Generic;
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

// 处理一个资产中所有被材质引用的图像（1.5：覆盖全部 PBR 贴图槽）。
TextureImportResult TextureImport::Import(const SceneAsset& asset,
                                          TextureCache& cache,
                                          Render& renderer) {
    TextureImportResult result;

    // 收集 (纹理索引 -> 用途)。同一索引多次出现时以首次用途决定颜色空间。
    // glTF 中每个 texture 通常专用于一个槽位，冲突属异常，给出提示并复用首次结果。
    std::vector<std::pair<int, MaterialTextureUsage>> referenced;
    std::unordered_map<int, MaterialTextureUsage> usageByTexture;
    auto addUsage = [&](int textureIndex, MaterialTextureUsage usage) {
        if (textureIndex < 0 || textureIndex >= static_cast<int>(asset.textures.size())) {
            return;
        }
        const auto found = usageByTexture.find(textureIndex);
        if (found == usageByTexture.end()) {
            usageByTexture.emplace(textureIndex, usage);
            referenced.emplace_back(textureIndex, usage);
        } else if (found->second != usage) {
            // 同一纹理被用于不同颜色空间的槽位：保留首次语义，避免重复上传。
            ++result.stats.failed;
            result.stats.notes.push_back(
                "纹理 " + std::to_string(textureIndex) +
                " 被用于不同颜色空间的槽位，已按首次用途处理");
        }
    };

    for (const Material& material : asset.materials) {
        addUsage(material.baseColorTexture, MaterialTextureUsage::BaseColor);
        addUsage(material.metallicRoughnessTexture, MaterialTextureUsage::MetallicRoughness);
        addUsage(material.normalTexture, MaterialTextureUsage::Normal);
        addUsage(material.occlusionTexture, MaterialTextureUsage::Occlusion);
        addUsage(material.emissiveTexture, MaterialTextureUsage::Emissive);
    }

    for (const auto& entry : referenced) {
        const int textureIndex = entry.first;
        const MaterialTextureUsage usage = entry.second;
        ++result.stats.referenced;

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
        const TextureSemantic semantic = SemanticForUsage(static_cast<int>(usage));
        // GPU 复用键需区分颜色空间：同一图像作为 sRGB 与线性贴图时应各自上传。
        if (!cacheKey.empty()) {
            cacheKey += IsSrgbSemantic(semantic) ? ":srgb" : ":linear";
        }

        TextureDesc desc;
        desc.width = decoded->width;
        desc.height = decoded->height;
        desc.semantic = semantic;
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
