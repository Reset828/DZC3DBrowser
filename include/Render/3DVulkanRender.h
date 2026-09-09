#ifndef __3D_VULKAN_RENDER_H__
#define __3D_VULKAN_RENDER_H__

#include "VulkanRender.h"
#include "VertexType/VertexTypes.h"
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include <string>
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
    void SetGrayEnabled(bool enabled);
    void SetDyeEnabled(bool enabled);
    void SetLightAnalysisEnabled(bool enabled);
    void SetShadowTextureSize(uint32_t size);
    uint32_t GetShadowTextureSize() const;
    bool IsShadowMapReady() const;
    std::string TakeShadowMapStatus();
    void SetShadowSceneBounds(const Vec3& boundsMin, const Vec3& boundsMax, bool valid);
    bool BeginShadowPass();
    void EndShadowPass();
    VkPipeline GetShadowPipeline() const override;
    void SetLatitude(float latitude);
    void SetLightDate(int year, int month, int day);
    void SetLightTimeMinutes(int minutes);
    glm::vec3 GetSunDirection() const;
    bool IsSunAboveHorizon() const;
    void SetOrthographicEnabled(bool enabled);
    void SetOrbitCenter(const Vec3& normalizedCenter);
    void ResetView(float orbitDistance = 3.0f);
    void SetCoordinateNormalization(const Vec3& sourceCenter, float normalizationScale);
    bool IsWireframeEnabled() const override { return m_wireframeMode; }

    void RequestCoordReadback(float ndcX, float ndcY);
    bool HasNewWorldCoord() const;
    float GetLastWorldX() const;
    float GetLastWorldY() const;
    float GetLastWorldZ() const;

protected:
    bool OnInitialize() override;
    void OnShutdown() override;
    void OnPrepareFrame() override;
    void OnBeginFrame() override;
    void OnEndFrame() override;
    void OnDestroyPipelines() override;
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
    void UpdateSunDirection();
    void UpdateShadowDescriptors();
    bool CreateShadowSampler();
    bool CreateDummyShadowMap();
    bool EnsureDummyShadowReady();
    bool CreateShadowRenderPass();
    bool CreateShadowPipeline();
    bool CreateShadowMap(uint32_t size);
    void DestroyShadowMap();
    void DestroyDummyShadowMap();
    void DestroyShadowSupport();
    void TransitionDepthImage(VkImage image, VkImageLayout oldLayout, VkImageLayout newLayout,
                              VkAccessFlags srcAccess, VkAccessFlags dstAccess,
                              VkPipelineStageFlags srcStage, VkPipelineStageFlags dstStage);
    bool EnsureShadowMapForAnalysis();
    bool TryAllocateShadowMap(uint32_t size);
    glm::mat4 ComputeLightViewProj() const;
    bool ShouldRenderShadows() const;
    void SetShadowMapStatus(const std::string& message);
    glm::vec3 ProjectToVirtualSphere(float nx, float ny) const;
    void ApplyConstrainedLocalRotation(const glm::vec3& localAxis, float angle);
    static void InitIdentityMatrix(float mat[4][4]);

    bool CreateDepthResources();
    void DestroyDepthResources();
    VkFormat FindDepthFormat();
    VkFormat FindShadowDepthFormat();
    VkFormat FindSupportedFormat(const std::vector<VkFormat>& candidates,
                                  VkImageTiling tiling,
                                  VkFormatFeatureFlags features);
    bool HasStencilComponent(VkFormat format);

    bool m_wireframeMode = false;
    bool m_grayEnabled = false;
    bool m_dyeEnabled = false;
    bool m_lightAnalysisEnabled = false;
    uint32_t m_shadowTextureSize = 2048;
    uint32_t m_allocatedShadowTextureSize = 0;
    bool m_shadowMapReady = false;
    bool m_dummyShadowReady = false;
    bool m_shadowPassActive = false;
    bool m_shadowBoundsValid = false;
    glm::vec3 m_shadowBoundsMin = glm::vec3(-1.0f);
    glm::vec3 m_shadowBoundsMax = glm::vec3(1.0f);
    glm::mat4 m_lightViewProj = glm::mat4(1.0f);
    std::string m_shadowMapStatus;
    float m_latitude = 36.0f;
    int m_lightYear = 2000;
    int m_lightMonth = 3;
    int m_lightDay = 20;
    int m_lightTimeMinutes = 360;
    glm::vec3 m_sunDirection = glm::vec3(0.0f, -1.0f, 0.0f);
    bool m_sunAboveHorizon = true;
    bool m_orthographicEnabled = false;

    glm::quat m_modelRotation = glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
    glm::vec3 m_orbitCenter = glm::vec3(0.0f);
    glm::vec3 m_panOffset = glm::vec3(0.0f);
    float m_orbitDistance = 3.0f;
    int m_mouseButton = -1;
    glm::vec2 m_lastMouse = glm::vec2(0.0f);
    glm::vec3 m_lastVerticalLocalAxis = glm::vec3(1.0f, 0.0f, 0.0f);

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
    glm::mat4 m_frameRenderToSource[MAX_FRAMES_IN_FLIGHT] = {};
    glm::mat4 m_normalizedToWorld = glm::mat4(1.0f);

protected:
    VkImage m_depthImage = VK_NULL_HANDLE;
    VkDeviceMemory m_depthImageMemory = VK_NULL_HANDLE;
    VkImageView m_depthImageView = VK_NULL_HANDLE;

    VkDescriptorSetLayout m_descriptorSetLayout = VK_NULL_HANDLE;
    VkDescriptorPool m_descriptorPool = VK_NULL_HANDLE;
    std::vector<VkDescriptorSet> m_descriptorSets;
    std::vector<VkDescriptorSet> m_shadowDescriptorSets;
    std::vector<VkBuffer> m_uniformBuffers;
    std::vector<VkDeviceMemory> m_uniformBuffersMemory;
    std::vector<void*> m_uniformBuffersMapped;

    VkSampler m_shadowSampler = VK_NULL_HANDLE;
    VkRenderPass m_shadowRenderPass = VK_NULL_HANDLE;
    VkPipeline m_shadowPipeline = VK_NULL_HANDLE;
    VkImage m_dummyShadowImage = VK_NULL_HANDLE;
    VkDeviceMemory m_dummyShadowMemory = VK_NULL_HANDLE;
    VkImageView m_dummyShadowView = VK_NULL_HANDLE;
    VkImage m_shadowImage = VK_NULL_HANDLE;
    VkDeviceMemory m_shadowMemory = VK_NULL_HANDLE;
    VkImageView m_shadowView = VK_NULL_HANDLE;
    VkFramebuffer m_shadowFramebuffer = VK_NULL_HANDLE;
    VkFormat m_shadowDepthFormat = VK_FORMAT_UNDEFINED;
};

#endif //__3D_VULKAN_RENDER_H__
