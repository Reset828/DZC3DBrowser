#ifndef __VK_TEXTURE_H__
#define __VK_TEXTURE_H__

#include <vulkan/vulkan.h>
#include <cstdint>

#include "Texture/TextureTypes.h"

// 创建一张 Vulkan 纹理所需的外部资源（由渲染器提供，本类不拥有）。
struct VKTextureContext {
    VkDevice device = VK_NULL_HANDLE;
    VkPhysicalDevice physicalDevice = VK_NULL_HANDLE;
    VkCommandPool commandPool = VK_NULL_HANDLE;  // 一次性上传/布局转换用
    VkQueue queue = VK_NULL_HANDLE;              // 提交一次性命令用
};

// 一张 Vulkan 2D 纹理：Image + 内存 + ImageView + Sampler。
// 负责 staging 上传、图像布局转换与 mipmap 生成。
// 不拥有 VkDevice / VkCommandPool（由渲染器持有）。
class VKTexture {
public:
    VKTexture() = default;
    ~VKTexture();

    VKTexture(const VKTexture&) = delete;
    VKTexture& operator=(const VKTexture&) = delete;

    // 按描述创建纹理；成功返回 true。
    bool Create(const VKTextureContext& context, const TextureDesc& desc);
    // 释放全部 GPU 资源；可安全重复调用。
    void Destroy();

    // 查询是否已成功创建。
    bool IsValid() const { return m_image != VK_NULL_HANDLE; }

    VkImage GetImage() const { return m_image; }
    VkImageView GetImageView() const { return m_view; }
    VkSampler GetSampler() const { return m_sampler; }
    VkFormat GetFormat() const { return m_format; }
    uint32_t GetWidth() const { return m_width; }
    uint32_t GetHeight() const { return m_height; }
    uint32_t GetMipLevels() const { return m_mipLevels; }

private:
    // 选择存储格式（颜色纹理用 sRGB，数据纹理用线性）。
    static VkFormat ChooseFormat(TextureSemantic semantic);
    // 选择内存类型索引。
    uint32_t FindMemoryType(uint32_t typeFilter, VkMemoryPropertyFlags properties) const;
    // 创建图像对象与设备内存。
    bool CreateImage(const TextureDesc& desc, VkFormat format, uint32_t mipLevels);
    // 创建图像视图。
    bool CreateImageView(uint32_t mipLevels);
    // 按采样器描述创建 VkSampler。
    bool CreateSampler(const SamplerDesc& sampler, bool useMipmaps);
    // 把 RGBA8 像素经 staging 缓冲区上传到 mip 0，并把全部 mip 级转为 TRANSFER_DST。
    bool UploadPixels(const TextureDesc& desc, uint32_t mipLevels);
    // 由 mip 0 通过 blit 逐级生成完整 mip 链。
    bool GenerateMipmaps(uint32_t mipLevels);
    // 转换图像布局（一次性命令）。
    void TransitionLayout(VkImageLayout oldLayout, VkImageLayout newLayout,
                          uint32_t baseMip, uint32_t mipCount);
    // 开始一次性命令。
    VkCommandBuffer BeginSingleTimeCommands() const;
    // 结束并提交一次性命令。
    void EndSingleTimeCommands(VkCommandBuffer commandBuffer) const;

    VkDevice m_device = VK_NULL_HANDLE;
    VkPhysicalDevice m_physicalDevice = VK_NULL_HANDLE;
    VkCommandPool m_commandPool = VK_NULL_HANDLE;
    VkQueue m_queue = VK_NULL_HANDLE;

    VkImage m_image = VK_NULL_HANDLE;
    VkDeviceMemory m_memory = VK_NULL_HANDLE;
    VkImageView m_view = VK_NULL_HANDLE;
    VkSampler m_sampler = VK_NULL_HANDLE;

    VkFormat m_format = VK_FORMAT_UNDEFINED;
    uint32_t m_width = 0;
    uint32_t m_height = 0;
    uint32_t m_mipLevels = 1;
};

#endif //__VK_TEXTURE_H__
