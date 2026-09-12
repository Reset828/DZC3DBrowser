#ifndef __VK_RENDER_H__
#define __VK_RENDER_H__


#include <vulkan/vulkan.h>
#include <vector>
#include <unordered_map>
#include <string>
#include <cstdint>
#include <cstring>

#include "Render.h"

class QRunnable;  // Qt 线程池任务（仅指针使用，可前向声明）


struct VulkanQueueFamilyIndices {
    int graphicsFamily = -1;  // 图形队列族索引
    int presentFamily = -1;   // 呈现队列族索引
    bool IsComplete() const;
};

struct VulkanSwapchainSupportDetails {
    VkSurfaceCapabilitiesKHR capabilities;        // 表面能力
    std::vector<VkSurfaceFormatKHR> formats;      // 支持的表面格式
    std::vector<VkPresentModeKHR> presentModes;   // 支持的呈现模式
};

class VKRender : public Render {
public:
    VKRender();
    ~VKRender() override;

    VKRender(const VKRender&) = delete;
    VKRender& operator=(const VKRender&) = delete;

    bool Initialize(const char* appName, uint32_t width, uint32_t height) override;
    void Shutdown() override;
    void Quiesce() override;
    bool BeginFrame() override;                                    // 开始帧渲染，获取交换链图像
    void EndFrame() override;                                      // 结束帧渲染，提交命令缓冲区
    void DrawIndexed(uint32_t indexCount, uint32_t instanceCount = 1) override;  // 索引绘制
    virtual VkPipeline GetShadowPipeline() const { return VK_NULL_HANDLE; }

    VkDevice GetDevice() const;
    VkPhysicalDevice GetPhysicalDevice() const;
    VkCommandBuffer GetCurrentCommandBuffer() const;


    VkBuffer CreateBuffer(VkDeviceSize size, VkBufferUsageFlags usage, VkMemoryPropertyFlags properties);
    void DestroyBuffer(VkBuffer buffer);
    VkResult MapBuffer(VkBuffer buffer, void** data);
    void UnmapBuffer(VkBuffer buffer);
    VkDeviceMemory AllocateMemory(VkMemoryRequirements memRequirements, VkMemoryPropertyFlags properties);

    VkCommandBuffer BeginSingleTimeCommands();
    void EndSingleTimeCommands(VkCommandBuffer commandBuffer);

    void WaitForIdle() override;

    void SubmitAsync(Render::AsyncTask task) override;
    void SubmitAsync(QRunnable* task);
    VkPipeline GetPipeline(DrawTopology topology) const;

    void SetSurface(VkSurfaceKHR surface);
    void SetInstance(VkInstance instance);
    static bool CheckValidationLayerSupport();        // 检查验证层支持
    static std::vector<const char*> GetRequiredExtensions();  // 获取所需扩展

protected:
    virtual bool OnInitialize() { return true; }
    virtual void OnShutdown() {}

    virtual void OnPrepareFrame() {}
    virtual void OnBeginFrame() {}
    virtual void OnEndFrame() {}
    virtual void OnDestroyPipelines() {}

    virtual void OnRecreateSwapchain() {}

    bool EnsureFrameRecording();
    void BeginColorRenderPass();

    virtual bool CreateRenderPass();
    virtual bool CreatePipelines();
    virtual bool CreateFramebuffers();

protected:
    bool CreateInstance(const char* appName);         // 创建Vulkan实例
    bool SetupDebugMessenger();                       // 设置调试消息回调
    bool PickPhysicalDevice();                        // 选择物理设备
    bool CreateLogicalDevice();                       // 创建逻辑设备
    bool CreateSwapchain();                           // 创建交换链
    bool RecreateSwapchain();                         // 重建交换链（窗口大小变化时）
    bool CreateImageViews();                          // 创建图像视图
    bool CreateCommandPool();                         // 创建命令池
    bool CreateCommandBuffers();                      // 创建命令缓冲区
    bool CreateSyncObjects();                         // 创建同步对象（信号量、围栏）

    void CleanupSwapchain();                          // 清理交换链相关资源

    VulkanQueueFamilyIndices FindQueueFamilies(VkPhysicalDevice device);  // 查找队列族
    VulkanSwapchainSupportDetails QuerySwapchainSupport(VkPhysicalDevice device);  // 查询交换链支持
    bool IsDeviceSuitable(VkPhysicalDevice device);   // 设备是否适合
    int RateDevice(VkPhysicalDevice device);          // 设备评分

    VkSurfaceFormatKHR ChooseSwapSurfaceFormat(const std::vector<VkSurfaceFormatKHR>& availableFormats);
    VkPresentModeKHR ChooseSwapPresentMode(const std::vector<VkPresentModeKHR>& availablePresentModes);
    VkExtent2D ChooseSwapExtent(const VkSurfaceCapabilitiesKHR& capabilities);

    uint32_t FindMemoryType(uint32_t typeFilter, VkMemoryPropertyFlags properties);

    VkResult CreateShaderModule(const std::vector<char>& code, VkShaderModule* shaderModule);

    std::vector<char> ReadShaderFile(const std::string& filename);
    VkShaderModule CreateShaderModuleHelper(const std::vector<char>& code);

    static VKAPI_ATTR VkBool32 VKAPI_CALL DebugCallback(
        VkDebugUtilsMessageSeverityFlagBitsEXT severity,
        VkDebugUtilsMessageTypeFlagsEXT type,
        const VkDebugUtilsMessengerCallbackDataEXT* pCallbackData,
        void* pUserData);

protected:

    VkInstance m_instance = VK_NULL_HANDLE;             // Vulkan实例
    VkDebugUtilsMessengerEXT m_debugMessenger = VK_NULL_HANDLE;  // 调试消息回调
    VkSurfaceKHR m_surface = VK_NULL_HANDLE;            // 窗口表面
    VkPhysicalDevice m_physicalDevice = VK_NULL_HANDLE; // 物理设备
    VkDevice m_device = VK_NULL_HANDLE;                 // 逻辑设备

    VkQueue m_graphicsQueue = VK_NULL_HANDLE;  // 图形队列
    VkQueue m_presentQueue = VK_NULL_HANDLE;   // 呈现队列

    VkSwapchainKHR m_swapchain = VK_NULL_HANDLE;        // 交换链
    std::vector<VkImage> m_swapchainImages;             // 交换链图像
    VkFormat m_swapchainImageFormat;                    // 交换链图像格式
    VkExtent2D m_swapchainExtent;                       // 交换链尺寸
    std::vector<VkImageView> m_swapchainImageViews;     // 交换链图像视图
    std::vector<VkFramebuffer> m_swapchainFramebuffers; // 交换链帧缓冲区

    VkRenderPass m_renderPass = VK_NULL_HANDLE;                // 渲染通道
    VkPipelineLayout m_pipelineLayout = VK_NULL_HANDLE;        // 管线布局
    VkPipeline m_pipelines[DT_COUNT] = {};                     // 多拓扑管线数组

    VkClearValue m_clearValues[2] = {};
    uint32_t m_clearValueCount = 1;

    VkCommandPool m_commandPool = VK_NULL_HANDLE;             // 命令池（渲染帧）
    VkCommandPool m_singleTimeCommandPool = VK_NULL_HANDLE;   // 单次命令专用池
    std::vector<VkCommandBuffer> m_commandBuffers;            // 命令缓冲区数组

    std::unordered_map<VkBuffer, VkDeviceMemory> m_bufferMemoryMap;

    std::vector<VkSemaphore> m_imageAvailableSemaphores;  // 图像可用信号量
    std::vector<VkSemaphore> m_renderFinishedSemaphores;  // 渲染完成信号量
    std::vector<VkFence> m_inFlightFences;                // 飞行中围栏
    uint32_t m_currentFrame = 0;                          // 当前帧索引
    uint32_t m_imageIndex = 0;                            // 当前交换链图像索引
    static const int MAX_FRAMES_IN_FLIGHT = 2;           // 最大飞行帧数（双缓冲）
    bool m_frameRecording = false;
    bool m_externalInstance = false;    // 实例由外部管理（Qt 窗口）

    std::vector<const char*> m_validationLayers = {
        "VK_LAYER_KHRONOS_validation"  // Khronos验证层
    };

    std::vector<const char*> m_deviceExtensions = {
        VK_KHR_SWAPCHAIN_EXTENSION_NAME  // 交换链扩展
    };

#ifdef NDEBUG
    const bool m_enableValidationLayers = false;  // 发布模式禁用验证层
#else
    const bool m_enableValidationLayers = true;   // 调试模式启用验证层
#endif
};



#include "VertexType/VertexTypes.h"
#include <glm/glm.hpp>
#include <vector>

struct CameraUBO2D {
    alignas(16) glm::mat4 projView;
};

class VKRender2D : public VKRender {
public:
    VKRender2D();
    ~VKRender2D() override;

    VKRender2D(const VKRender2D&) = delete;
    VKRender2D& operator=(const VKRender2D&) = delete;

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

#include "VertexType/VertexTypes.h"
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include <string>
#include <vector>

class VKRender3D : public VKRender {
public:
    VKRender3D();
    ~VKRender3D() override;

    VKRender3D(const VKRender3D&) = delete;
    VKRender3D& operator=(const VKRender3D&) = delete;

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

#endif //__VK_RENDER_H__
