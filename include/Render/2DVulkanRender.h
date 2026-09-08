#ifndef __2D_VULKAN_RENDER_H__
#define __2D_VULKAN_RENDER_H__

#include "VulkanRender.h"
#include "VertexType/VertexTypes.h"
#include <glm/glm.hpp>
#include <vector>

struct CameraUBO2D {
    alignas(16) glm::mat4 projView;
};

class VulkanRender2D : public VulkanRender {
public:
    VulkanRender2D();
    ~VulkanRender2D() override;

    VulkanRender2D(const VulkanRender2D&) = delete;
    VulkanRender2D& operator=(const VulkanRender2D&) = delete;

    void OnMouseDown(float nx, float ny, int button) override;
    void OnMouseMove(float nx, float ny) override;
    void OnMouseUp(int button) override;
    void OnMouseWheel(float delta) override;

protected:
    bool OnInitialize() override;
    void OnShutdown() override;
    void OnBeginFrame() override;
    void OnEndFrame() override;
    bool CreatePipelines() override;

    bool CreateDescriptorSetLayout();
    bool CreateUniformBuffers();
    bool CreateDescriptorPool();
    bool CreateDescriptorSets();
    void DestroyUniformBuffers();

    void UpdateCameraUBO();

protected:
    glm::vec2 m_panOffset = glm::vec2(0.0f);
    float m_zoomLevel = 1.0f;

    int m_mouseButton = -1;
    glm::vec2 m_lastMouse = glm::vec2(0.0f);

    VkDescriptorSetLayout m_descriptorSetLayout = VK_NULL_HANDLE;
    VkDescriptorPool m_descriptorPool = VK_NULL_HANDLE;
    std::vector<VkDescriptorSet> m_descriptorSets;
    std::vector<VkBuffer> m_uniformBuffers;
    std::vector<VkDeviceMemory> m_uniformBuffersMemory;
    std::vector<void*> m_uniformBuffersMapped;
};

#endif //__2D_VULKAN_RENDER_H__
