#ifndef __3D_VULKAN_RENDER_H__
#define __3D_VULKAN_RENDER_H__

#include "VulkanRender.h"
#include "VertexTypes.h"
#include <glm/glm.hpp>
#include <vector>

class VulkanRender3D : public VulkanRender {
public:
    VulkanRender3D();
    ~VulkanRender3D() override;

    VulkanRender3D(const VulkanRender3D&) = delete;
    VulkanRender3D& operator=(const VulkanRender3D&) = delete;

    void OnMouseDown(float nx, float ny, int button) override;
    void OnMouseMove(float nx, float ny) override;
    void OnMouseUp(int button) override;
    void OnMouseWheel(float delta) override;

protected:
    bool OnInitialize() override;
    void OnShutdown() override;
    void OnBeginFrame() override;
    void OnRecreateSwapchain() override;

    bool CreateRenderPass() override;
    bool CreatePipelines() override;
    bool CreateFramebuffers() override;

    bool CreateDescriptorSetLayout();
    bool CreateUniformBuffers();
    bool CreateDescriptorPool();
    bool CreateDescriptorSets();
    void DestroyUniformBuffers();
    void UpdateUniformBuffer(uint32_t currentImage);
    static void InitIdentityMatrix(float mat[4][4]);

    bool CreateDepthResources();
    void DestroyDepthResources();
    VkFormat FindDepthFormat();
    VkFormat FindSupportedFormat(const std::vector<VkFormat>& candidates,
                                  VkImageTiling tiling,
                                  VkFormatFeatureFlags features);
    bool HasStencilComponent(VkFormat format);

protected:
    // 轨道相机
    glm::vec3 m_orbitTarget = glm::vec3(0.0f);
    float m_orbitDistance = 3.0f;
    float m_orbitTheta = 0.0f;
    float m_orbitPhi = 0.0f;

    int m_mouseButton = -1;
    glm::vec2 m_lastMouse = glm::vec2(0.0f);

    // 深度缓冲
    VkImage m_depthImage = VK_NULL_HANDLE;
    VkDeviceMemory m_depthImageMemory = VK_NULL_HANDLE;
    VkImageView m_depthImageView = VK_NULL_HANDLE;

    // 描述符集
    VkDescriptorSetLayout m_descriptorSetLayout = VK_NULL_HANDLE;
    VkDescriptorPool m_descriptorPool = VK_NULL_HANDLE;
    std::vector<VkDescriptorSet> m_descriptorSets;
    std::vector<VkBuffer> m_uniformBuffers;
    std::vector<VkDeviceMemory> m_uniformBuffersMemory;
    std::vector<void*> m_uniformBuffersMapped;
};

#endif //__3D_VULKAN_RENDER_H__
