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
    void SetWireframeEnabled(bool enabled);
    bool IsWireframeEnabled() const override { return m_wireframeMode; }

    void RequestCoordReadback(float ndcX, float ndcY);
    bool HasNewWorldCoord() const;
    float GetLastWorldX() const { return m_lastWorldCoord[0]; }
    float GetLastWorldY() const { return m_lastWorldCoord[1]; }
    float GetLastWorldZ() const { return m_lastWorldCoord[2]; }

protected:
    bool OnInitialize() override;
    void OnShutdown() override;
    void OnBeginFrame() override;
    void OnEndFrame() override;
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

    // 线框模式
    bool m_wireframeMode = false;

    // 深度回读（世界坐标拾取）
    bool CreateDepthReadbackResources();
    void DestroyDepthReadbackResources();
    void ProcessDepthReadback(uint32_t frameIndex);

    bool m_depthReadbackRequested = false;
    float m_requestedNDCX = 0.0f;
    float m_requestedNDCY = 0.0f;
    bool m_pendingReadback[MAX_FRAMES_IN_FLIGHT] = {};
    float m_pendingNDCX[MAX_FRAMES_IN_FLIGHT] = {};
    float m_pendingNDCY[MAX_FRAMES_IN_FLIGHT] = {};
    VkBuffer m_depthReadbackBuffer[MAX_FRAMES_IN_FLIGHT] = {};
    VkDeviceMemory m_depthReadbackMemory[MAX_FRAMES_IN_FLIGHT] = {};
    void* m_depthReadbackMapped[MAX_FRAMES_IN_FLIGHT] = {};
    float m_lastWorldCoord[3] = {};
    mutable bool m_newCoordAvailable = false;
    glm::mat4 m_frameInvViewProj[MAX_FRAMES_IN_FLIGHT] = {};

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
