#ifndef __VK_RENDER_H__
#define __VK_RENDER_H__


#include <vulkan/vulkan.h>
#include <vector>
#include <unordered_map>
#include <string>
#include <cstdint>
#include <cstring>
#include <memory>
#include <utility>

#include "Render.h"

class QRunnable;  // Qt 线程池任务（仅指针使用，可前向声明）
class QThreadPool;
class VKTexture;  // GPU 纹理资源（include/Texture/VKTexture.h）


struct VulkanQueueFamilyIndices {
    int graphicsFamily = -1;  // 图形队列族索引
    int presentFamily = -1;   // 呈现队列族索引
    // 图形与呈现队列族是否都已找到。
    bool IsComplete() const;
};

struct VulkanSwapchainSupportDetails {
    VkSurfaceCapabilitiesKHR capabilities;        // 表面能力
    std::vector<VkSurfaceFormatKHR> formats;      // 支持的表面格式
    std::vector<VkPresentModeKHR> presentModes;   // 支持的呈现模式
};

// Vulkan 后端：设备、交换链、同步和帧流程。
class VKRender : public Render {
public:
    VKRender();
    ~VKRender() override;

    VKRender(const VKRender&) = delete;
    VKRender& operator=(const VKRender&) = delete;

    // 初始化渲染器及其后端资源。
    bool Initialize(const char* appName, uint32_t width, uint32_t height) override;
    // 关闭渲染器并释放资源。
    void Shutdown() override;
    // 等待异步任务完成并使渲染器静止。
    void Quiesce() override;
    // 开始一帧渲染。
    bool BeginFrame() override;
    // 结束当前帧并提交结果。
    void EndFrame() override;
    // 提交索引绘制命令。
    void DrawIndexed(uint32_t indexCount, uint32_t instanceCount = 1) override;
    // 返回阴影图形管线。
    virtual VkPipeline GetShadowPipeline() const { return VK_NULL_HANDLE; }

    // 创建一张 GPU 纹理。返回句柄；0 表示失败。
    TextureHandle CreateTexture(const TextureDesc& desc) override;
    // 标记释放纹理，实际销毁延迟到帧计数队列。
    void DestroyTexture(TextureHandle handle) override;
    // 推进延迟销毁队列。
    void ProcessDeferredTextureDestruction() override;
    // 立即销毁全部纹理。
    void ReleaseAllTextures() override;
    // 查询句柄是否有效。
    bool IsTextureValid(TextureHandle handle) const override;
    // 某纹理真正销毁时的回调（子类可清理其描述符集等派生资源）。
    virtual void OnTextureDestroyed(TextureHandle handle) { (void)handle; }
    // 返回纹理的 VkImageView（供未来 descriptor 绑定）；无效返回 VK_NULL_HANDLE。
    VkImageView GetTextureImageView(TextureHandle handle) const;
    // 返回纹理的 VkSampler；无效返回 VK_NULL_HANDLE。
    VkSampler GetTextureSampler(TextureHandle handle) const;
    // 返回 Vulkan 逻辑设备。
    VkDevice GetDevice() const;
    // 返回 Vulkan 物理设备。
    VkPhysicalDevice GetPhysicalDevice() const;
    // 返回当前帧命令缓冲。
    VkCommandBuffer GetCurrentCommandBuffer() const;

    // 提交带起始索引的索引绘制（SubMesh 范围）。
    void DrawIndexedRange(uint32_t indexCount, uint32_t firstIndex,
                          uint32_t vertexOffset = 0) override;


    // 创建 Vulkan 缓冲区并绑定内存。
    VkBuffer CreateBuffer(VkDeviceSize size, VkBufferUsageFlags usage, VkMemoryPropertyFlags properties);
    // 销毁缓冲区及其设备内存。
    void DestroyBuffer(VkBuffer buffer);
    // 映射 Vulkan 缓冲区内存。
    VkResult MapBuffer(VkBuffer buffer, void** data);
    // 解除 Vulkan 缓冲区内存映射。
    void UnmapBuffer(VkBuffer buffer);
    // 分配 Vulkan 设备内存。
    VkDeviceMemory AllocateMemory(VkMemoryRequirements memRequirements, VkMemoryPropertyFlags properties);

    // 开始一次性命令缓冲。
    VkCommandBuffer BeginSingleTimeCommands();
    // 提交并等待一次性命令缓冲。
    void EndSingleTimeCommands(VkCommandBuffer commandBuffer);
    // 复制 Vulkan 缓冲区内容。
    void CopyBuffer(VkBuffer srcBuffer, VkBuffer dstBuffer, VkDeviceSize size,
                    VkDeviceSize srcOffset = 0, VkDeviceSize dstOffset = 0);
    // 从 staging 缓冲区创建设备缓冲区。
    VkBuffer CreateBufferFromStaging(VkBuffer stagingBuffer, VkDeviceSize size,
                                     VkBufferUsageFlags usage,
                                     VkDeviceSize stagingOffset = 0);

    // 等待 GPU 与异步任务完成。
    void WaitForIdle() override;

    // 把任务丢进线程池异步执行。
    void SubmitAsync(Render::AsyncTask task) override;
    // 把任务丢进线程池异步执行。
    void SubmitAsync(QRunnable* task);
    // 按拓扑返回图形管线。
    VkPipeline GetPipeline(DrawTopology topology) const;

    // 设置 Win32 表面（外部所有）。
    void SetSurface(VkSurfaceKHR surface);
    // 设置外部 VkInstance。
    void SetInstance(VkInstance instance);
    // 检查 Vulkan 验证层支持。
    static bool CheckValidationLayerSupport();
    // 返回实例所需扩展名。
    static std::vector<const char*> GetRequiredExtensions();

protected:
    // 后端初始化完成后的钩子。
    virtual bool OnInitialize() { return true; }
    // 关闭前释放后端资源的钩子。
    virtual void OnShutdown() {}

    // 开始录制命令前的钩子。
    virtual void OnPrepareFrame() {}
    // 每帧开始时的钩子。
    virtual void OnBeginFrame() {}
    // 每帧结束时的钩子。
    virtual void OnEndFrame() {}
    // 销毁图形管线的钩子。
    virtual void OnDestroyPipelines() {}

    // 交换链/帧缓冲重建后的钩子。
    virtual void OnRecreateSwapchain() {}

    // 确保本帧已开始录制命令。
    bool EnsureFrameRecording();
    // 开始主颜色渲染通道。
    void BeginColorRenderPass();

    // 创建渲染通道。
    virtual bool CreateRenderPass();
    // 创建图形管线。
    virtual bool CreatePipelines();
    // 创建帧缓冲。
    virtual bool CreateFramebuffers();

protected:
    // 创建 VkInstance。
    bool CreateInstance(const char* appName);
    // 创建验证层调试回调。
    bool SetupDebugMessenger();
    // 选择合适的 Vulkan 物理设备。
    bool PickPhysicalDevice();
    // 创建逻辑设备与队列。
    bool CreateLogicalDevice();
    // 创建交换链。
    bool CreateSwapchain();
    // 重建 Vulkan 交换链。
    bool RecreateSwapchain();
    // 为交换链图像创建视图。
    bool CreateImageViews();
    // 创建命令池。
    bool CreateCommandPool();
    // 分配每帧命令缓冲。
    bool CreateCommandBuffers();
    // 创建信号量与围栏。
    bool CreateSyncObjects();

    // 清理 Vulkan 交换链资源。
    void CleanupSwapchain();

    // 查找图形与呈现队列族。
    VulkanQueueFamilyIndices FindQueueFamilies(VkPhysicalDevice device);
    // 查询表面格式与呈现模式。
    VulkanSwapchainSupportDetails QuerySwapchainSupport(VkPhysicalDevice device);
    // 判断物理设备是否满足交换链需求。
    bool IsDeviceSuitable(VkPhysicalDevice device);
    // 评估 Vulkan 物理设备。
    int RateDevice(VkPhysicalDevice device);

    // 选择交换链表面格式。
    VkSurfaceFormatKHR ChooseSwapSurfaceFormat(const std::vector<VkSurfaceFormatKHR>& availableFormats);
    // 选择呈现模式。
    VkPresentModeKHR ChooseSwapPresentMode(const std::vector<VkPresentModeKHR>& availablePresentModes);
    // 选择交换链分辨率。
    VkExtent2D ChooseSwapExtent(const VkSurfaceCapabilitiesKHR& capabilities);

    // 按过滤条件查找内存类型索引。
    uint32_t FindMemoryType(uint32_t typeFilter, VkMemoryPropertyFlags properties);

    // 从 SPIR-V 创建着色器模块。
    VkResult CreateShaderModule(const std::vector<char>& code, VkShaderModule* shaderModule);

    // 读取着色器文件。
    std::vector<char> ReadShaderFile(const std::string& filename);
    // 创建着色器模块，失败则抛错。
    VkShaderModule CreateShaderModuleHelper(const std::vector<char>& code,
                                            const std::string& filename);
    // 当前帧缓冲宽高是否可用于绘制。
    bool HasUsableFramebuffer() const;
    // 记录失败并返回 false。
    bool Fail(const std::string& message);

    // 处理 Vulkan 调试回调。
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

    // GPU 纹理资源（句柄 -> VKTexture）。句柄从 1 开始递增。
    std::unordered_map<TextureHandle, std::unique_ptr<VKTexture>> m_textures;
    TextureHandle m_nextTextureHandle = 1;
    // 跨模型复用：cacheKey -> 已有句柄；以及每个句柄的引用计数。
    std::unordered_map<std::string, TextureHandle> m_textureKeyToHandle;
    std::unordered_map<TextureHandle, int> m_textureRefCount;
    // 延迟销毁队列：{句柄, 入队时的帧号}。等安全帧数后再真正销毁。
    std::vector<std::pair<TextureHandle, uint64_t>> m_deferredTextureDestruction;
    uint64_t m_textureFrameCounter = 0;
    static constexpr uint64_t kTextureDestroyDelayFrames = 3;

    std::vector<VkSemaphore> m_imageAvailableSemaphores;  // 图像可用信号量
    std::vector<VkSemaphore> m_renderFinishedSemaphores;  // 渲染完成信号量
    std::vector<VkFence> m_inFlightFences;                // 飞行中围栏
    uint32_t m_currentFrame = 0;                          // 当前帧索引
    uint32_t m_imageIndex = 0;                            // 当前交换链图像索引
    static const int MAX_FRAMES_IN_FLIGHT = 2;           // 最大飞行帧数（双缓冲）
    bool m_frameRecording = false;
    std::unique_ptr<QThreadPool> m_asyncThreadPool;
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

    // 处理鼠标按下。
    void OnMouseDown(float nx, float ny, int button) override;
    // 处理鼠标移动。
    void OnMouseMove(float nx, float ny) override;
    // 处理鼠标松开。
    void OnMouseUp(int button) override;
    // 处理滚轮缩放。
    void OnMouseWheel(float delta) override;

protected:
    // 后端初始化完成后的钩子。
    bool OnInitialize() override;
    // 关闭前释放后端资源的钩子。
    void OnShutdown() override;
    // 每帧开始时的钩子。
    void OnBeginFrame() override;
    // 每帧结束时的钩子。
    void OnEndFrame() override;
    // 创建图形管线。
    bool CreatePipelines() override;

    // 创建描述符集布局。
    bool CreateDescriptorSetLayout();
    // 创建并映射 UBO。
    bool CreateUniformBuffers();
    // 创建描述符池。
    bool CreateDescriptorPool();
    // 分配并写入描述符集。
    bool CreateDescriptorSets();
    // 销毁 UBO 及其内存。
    void DestroyUniformBuffers();

    // 写入二维相机 UBO。
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

    // 处理鼠标按下。
    void OnMouseDown(float nx, float ny, int button) override;
    // 处理鼠标移动。
    void OnMouseMove(float nx, float ny) override;
    // 处理鼠标松开。
    void OnMouseUp(int button) override;
    // 处理滚轮缩放。
    void OnMouseWheel(float delta) override;
    // 开关线框模式。
    void SetWireframeEnabled(bool enabled);
    // 开关灰度显示。
    void SetGrayEnabled(bool enabled);
    // 开关染色。
    void SetDyeEnabled(bool enabled);
    // 开关光照分析。
    void SetLightAnalysisEnabled(bool enabled);
    // PBR 调试覆盖（1.5）：enabled 时用 value 覆盖材质的 metallic/roughness/emissive。
    void SetMetallicOverride(bool enabled, float value);
    void SetRoughnessOverride(bool enabled, float value);
    void SetEmissiveOverride(bool enabled, float value);
    // 设置阴影贴图边长。
    void SetShadowTextureSize(uint32_t size);
    // 返回阴影贴图边长。
    uint32_t GetShadowTextureSize() const;
    // 查询阴影贴图是否可用。
    bool IsShadowMapReady() const;
    // 读取并清除阴影贴图状态。
    std::string TakeShadowMapStatus();
    // 设置阴影场景包围盒。
    void SetShadowSceneBounds(const Vec3& boundsMin, const Vec3& boundsMax, bool valid);
    // 开始向阴影贴图绘制。
    bool BeginShadowPass();
    // 结束阴影通道并恢复主帧缓冲。
    void EndShadowPass();
    // 返回阴影图形管线。
    VkPipeline GetShadowPipeline() const override;
    // 设置太阳计算用纬度。
    void SetLatitude(float latitude);
    // 设置太阳计算用日期。
    void SetLightDate(int year, int month, int day);
    // 设置真太阳时（分钟）。
    void SetLightTimeMinutes(int minutes);
    // 返回物体空间太阳方向。
    glm::vec3 GetSunDirection() const;
    // 查询太阳是否在地平线以上。
    bool IsSunAboveHorizon() const;
    // 开关正射投影。
    void SetOrthographicEnabled(bool enabled);
    // 设置轨道旋转中心。
    void SetOrbitCenter(const Vec3& normalizedCenter);
    // 复位旋转、平移和轨道距离。
    void ResetView(float orbitDistance = 3.0f);
    // 设置归一化坐标到世界坐标的变换。
    void SetCoordinateNormalization(const Vec3& sourceCenter, float normalizationScale);
    // 查询是否开启线框。
    bool IsWireframeEnabled() const override { return m_wireframeMode; }

    // 请求把屏幕点反算为世界坐标。
    void RequestCoordReadback(float ndcX, float ndcY);
    // 查询是否有新的世界坐标可读。
    bool HasNewWorldCoord() const;
    // 返回最近一次世界坐标 X。
    float GetLastWorldX() const;
    // 返回最近一次世界坐标 Y。
    float GetLastWorldY() const;
    // 返回最近一次世界坐标 Z。
    float GetLastWorldZ() const;

protected:
    // 后端初始化完成后的钩子。
    bool OnInitialize() override;
    // 关闭前释放后端资源的钩子。
    void OnShutdown() override;
    // 开始录制命令前的钩子。
    void OnPrepareFrame() override;
    // 每帧开始时的钩子。
    void OnBeginFrame() override;
    // 每帧结束时的钩子。
    void OnEndFrame() override;
    // 销毁图形管线的钩子。
    void OnDestroyPipelines() override;
    // 交换链/帧缓冲重建后的钩子。
    void OnRecreateSwapchain() override;

    // 创建渲染通道。
    bool CreateRenderPass() override;
    // 创建图形管线。
    bool CreatePipelines() override;
    // 创建帧缓冲。
    bool CreateFramebuffers() override;

    // 创建描述符集布局。
    bool CreateDescriptorSetLayout();
    // 创建材质（set 1）描述符布局与池。
    bool CreateMaterialDescriptors();
    // 销毁材质描述符资源。
    void DestroyMaterialDescriptors();
    // 创建并映射 UBO。
    bool CreateUniformBuffers();
    // 创建描述符池。
    bool CreateDescriptorPool();
    // 分配并写入描述符集。
    bool CreateDescriptorSets();
    // 销毁 UBO 及其内存。
    void DestroyUniformBuffers();
    // 按当前相机与显示选项写入 UBO。
    void UpdateUniformBuffer(uint32_t currentImage);
    // 按纬度/日期/真太阳时更新太阳方向。
    void UpdateSunDirection();
    // 把阴影贴图写入描述符集。
    void UpdateShadowDescriptors();
    // 创建阴影比较采样器。
    bool CreateShadowSampler();
    // 创建 1x1 占位阴影贴图。
    bool CreateDummyShadowMap();
    // 保证占位阴影图布局可用。
    bool EnsureDummyShadowReady();
    // 创建仅深度的阴影渲染通道。
    bool CreateShadowRenderPass();
    // 创建阴影图形管线。
    bool CreateShadowPipeline();
    // 按给定边长创建阴影深度贴图。
    bool CreateShadowMap(uint32_t size);
    // 销毁阴影贴图与帧缓冲。
    void DestroyShadowMap();
    // 销毁占位阴影贴图。
    void DestroyDummyShadowMap();
    // 销毁阴影采样器与渲染通道。
    void DestroyShadowSupport();
    // 转换深度图像布局。
    void TransitionDepthImage(VkImage image, VkImageLayout oldLayout, VkImageLayout newLayout,
                              VkAccessFlags srcAccess, VkAccessFlags dstAccess,
                              VkPipelineStageFlags srcStage, VkPipelineStageFlags dstStage);
    // 光照分析开启时保证阴影贴图可用。
    bool EnsureShadowMapForAnalysis();
    // 尝试分配指定尺寸的阴影贴图。
    bool TryAllocateShadowMap(uint32_t size);
    // 计算光源视图投影矩阵。
    glm::mat4 ComputeLightViewProj() const;
    // 判断当前是否需要渲染阴影。
    bool ShouldRenderShadows() const;
    // 记录阴影贴图状态文本。
    void SetShadowMapStatus(const std::string& message);
    // 将鼠标位置映射到虚拟球面。
    glm::vec3 ProjectToVirtualSphere(float nx, float ny) const;
    // 应用受约束的局部旋转。
    void ApplyConstrainedLocalRotation(const glm::vec3& localAxis, float angle);
    // 初始化单位矩阵。
    static void InitIdentityMatrix(float mat[4][4]);

    // 以材质参数（push constant）+ set 1（5 个纹理槽）绑定后绘制当前网格。
    void ApplyMaterial(const MaterialParams& params, const MaterialTextureSet& textures) override;
    // 按距离从远到近排序并绘制已收集的透明请求。
    void FlushTransparentDraws() override;
    // 计算点（上传坐标空间）到相机的距离。
    float ComputeDrawDistance(const float center[3]) const override;
    // 取得（或惰性分配）某组纹理对应的 set 1 描述符集。
    VkDescriptorSet MaterialDescriptorSetFor(const MaterialTextureSet& textures);
    // 确保默认白纹理存在（baseColor / metallicRoughness / occlusion / emissive 用），返回其句柄。
    TextureHandle EnsureDefaultWhiteTexture();
    // 确保默认平面法线纹理存在（无 normal 贴图时使用），返回其句柄。
    TextureHandle EnsureDefaultFlatNormalTexture();
    // 纹理销毁时回收引用它的材质描述符集。
    void OnTextureDestroyed(TextureHandle handle) override;

    // 创建深度附件。
    bool CreateDepthResources();
    // 销毁深度附件。
    void DestroyDepthResources();
    // 选择主深度格式。
    VkFormat FindDepthFormat();
    // 选择阴影深度格式。
    VkFormat FindShadowDepthFormat();
    // 在候选格式里找设备支持项。
    VkFormat FindSupportedFormat(const std::vector<VkFormat>& candidates,
                                  VkImageTiling tiling,
                                  VkFormatFeatureFlags features);
    // 查询格式是否含模板。
    bool HasStencilComponent(VkFormat format);

    bool m_wireframeMode = false;
    bool m_grayEnabled = false;
    bool m_dyeEnabled = false;
    bool m_lightAnalysisEnabled = false;
    // PBR 调试覆盖（1.5）。
    bool m_metallicOverrideEnabled = false;
    float m_metallicOverride = 0.0f;
    bool m_roughnessOverrideEnabled = false;
    float m_roughnessOverride = 0.5f;
    bool m_emissiveOverrideEnabled = false;
    float m_emissiveOverride = 0.0f;
    uint32_t m_shadowTextureSize = 2048;
    uint32_t m_allocatedShadowTextureSize = 0;
    bool m_shadowMapReady = false;
    bool m_dummyShadowReady = false;
    bool m_shadowPassActive = false;
    // 阴影贴图创建后，是否已至少渲染过一次并锁定光源矩阵。
    bool m_shadowMatrixValid = false;
    bool m_shadowBoundsValid = false;
    glm::vec3 m_shadowBoundsMin = glm::vec3(-1.0f);
    glm::vec3 m_shadowBoundsMax = glm::vec3(1.0f);
    glm::mat4 m_lightViewProj = glm::mat4(1.0f);
    // 上次渲染阴影贴图时锁定的光源矩阵；主绘制采样必须与之一致，避免贴图与矩阵错位。
    glm::mat4 m_shadowLightViewProj = glm::mat4(1.0f);
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

    // 创建深度回读缓冲。
    bool CreateDepthReadbackResources();
    // 销毁深度回读缓冲。
    void DestroyDepthReadbackResources();
    // 把回读深度反投影为世界坐标。
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

    // 材质（set 1）：每个纹理集合一个描述符集 + 默认白/平面法线纹理。
    // 描述符集缓存按纹理集合键（5 个句柄的组合）索引；同时记录其引用的句柄，
    // 以便纹理销毁时只回收引用它的描述符集（避免误释放仍在用的集合）。
    // set 1 纹理槽数量（baseColor / metallicRoughness / normal / occlusion / emissive）。
    static constexpr uint32_t kMaterialTextureSlotCount = 5;
    struct MaterialDescriptorSetEntry {
        VkDescriptorSet set = VK_NULL_HANDLE;
        TextureHandle handles[kMaterialTextureSlotCount] = {};
    };
    VkDescriptorSetLayout m_materialSetLayout = VK_NULL_HANDLE;
    VkDescriptorPool m_materialDescriptorPool = VK_NULL_HANDLE;
    TextureHandle m_defaultWhiteHandle = 0;
    TextureHandle m_defaultFlatNormalHandle = 0;
    std::unordered_map<std::string, MaterialDescriptorSetEntry> m_materialDescriptorSets;
    static constexpr uint32_t kMaterialDescriptorPoolSize = 512;

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
