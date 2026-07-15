#ifndef __VULKAN_RENDER_H__
#define __VULKAN_RENDER_H__


#include <vulkan/vulkan.h>
#include <vector>
#include <unordered_map>
#include <string>
#include <cstdint>
#include <cstring>
#include <atomic>

class QRunnable;  // Qt 线程池任务（仅指针使用，可前向声明）

// 基础数学类型
struct Vec2 { float x, y; };
struct Vec3 { float x, y, z; };
struct Vec4 { float x, y, z, w; };
struct Mat4 { float m[4][4]; };
struct Rect2D { int x, y, width, height; };

// 队列族索引（图形队列和呈现队列）
struct VulkanQueueFamilyIndices {
    int graphicsFamily = -1;  // 图形队列族索引
    int presentFamily = -1;   // 呈现队列族索引
    bool IsComplete() const;
};

// 交换链支持详情（能力、格式、呈现模式）
struct VulkanSwapchainSupportDetails {
    VkSurfaceCapabilitiesKHR capabilities;        // 表面能力
    std::vector<VkSurfaceFormatKHR> formats;      // 支持的表面格式
    std::vector<VkPresentModeKHR> presentModes;   // 支持的呈现模式
};

// 顶点数据结构（位置、颜色、纹理坐标）
struct VulkanVertex {
    Vec3 position;   // 顶点位置
    Vec3 color;      // 顶点颜色
    Vec2 texCoord;   // 纹理坐标

    static VkVertexInputBindingDescription GetBindingDescription();
    static std::vector<VkVertexInputAttributeDescription> GetAttributeDescriptions();
};

// 描述符集统一缓冲区对象（MVP矩阵）
struct UniformBufferObject {
    alignas(16) Mat4 model;  // 模型矩阵
    alignas(16) Mat4 view;   // 视图矩阵
    alignas(16) Mat4 proj;   // 投影矩阵
};

// Vulkan渲染器主类 - 管理完整的Vulkan渲染管线
class VulkanRender {
public:
    VulkanRender();
    ~VulkanRender();

    VulkanRender(const VulkanRender&) = delete;
    VulkanRender& operator=(const VulkanRender&) = delete;

    // 绘制拓扑类型
    enum DrawTopology {
        DT_TRIANGLE = 0,  // 三角形列表（多边形）
        DT_LINE,          // 线段列表
        DT_POINT,         // 点列表
        DT_COUNT
    };

    // 初始化和关闭
    bool Initialize(const char* appName, uint32_t width, uint32_t height);
    void Shutdown();
    bool IsInitialized() const { return m_initialized; }

    // 停止新异步任务并等待所有进行中的任务完成，但不销毁 Vulkan 资源
    // 用于在 Shutdown() 之前安全清理外部 Vulkan 对象
    void Quiesce();

    // 视口和清除颜色设置
    void SetViewport(const Rect2D& rc);
    void SetClearColor(float r, float g, float b, float a);

    // 帧渲染控制
    bool BeginFrame();                                    // 开始帧渲染，获取交换链图像
    void EndFrame();                                      // 结束帧渲染，提交命令缓冲区
    void DrawIndexed(uint32_t indexCount, uint32_t instanceCount = 1);  // 索引绘制

    // 获取Vulkan对象
    VkDevice GetDevice() const;
    VkPhysicalDevice GetPhysicalDevice() const;
    VkInstance GetInstance() const;
    VkCommandBuffer GetCurrentCommandBuffer() const;
    uint32_t GetCurrentFrame() const;
    uint32_t GetFramebufferWidth() const;
    uint32_t GetFramebufferHeight() const;

    // 缓冲区操作
    VkBuffer CreateBuffer(VkDeviceSize size, VkBufferUsageFlags usage, VkMemoryPropertyFlags properties);
    void DestroyBuffer(VkBuffer buffer);
    VkResult MapBuffer(VkBuffer buffer, void** data);
    void UnmapBuffer(VkBuffer buffer);
    VkDeviceMemory AllocateMemory(VkMemoryRequirements memRequirements, VkMemoryPropertyFlags properties);

    // 单次命令缓冲区（用于一次性操作）
    VkCommandBuffer BeginSingleTimeCommands();
    void EndSingleTimeCommands(VkCommandBuffer commandBuffer);

    // 等待设备空闲
    void WaitForIdle();

    // 提交异步任务到全局线程池
    void SubmitAsync(QRunnable* task);

    // 是否正在关闭（阻止关闭过程中产生的异步回调创建新任务）
    bool IsShuttingDown() const { return m_shuttingDown; }

    // 获取管线布局和渲染通道
    VkPipelineLayout GetPipelineLayout() const;
    VkRenderPass GetRenderPass() const;
    VkPipeline GetPipeline(DrawTopology topology) const;

    // 窗口表面设置
    void SetSurface(VkSurfaceKHR surface) { m_surface = surface; }
    void SetInstance(VkInstance instance) { m_instance = instance; m_externalInstance = true; }
    void SetFramebufferSize(uint32_t width, uint32_t height) { m_framebufferWidth = width; m_framebufferHeight = height; }
    void SetFramebufferResized(bool resized) { m_framebufferResized = resized; }

    // 静态工具函数
    static bool CheckValidationLayerSupport();        // 检查验证层支持
    static std::vector<const char*> GetRequiredExtensions();  // 获取所需扩展

protected:
    // Vulkan对象创建
    bool CreateInstance(const char* appName);         // 创建Vulkan实例
    bool SetupDebugMessenger();                       // 设置调试消息回调
    bool PickPhysicalDevice();                        // 选择物理设备
    bool CreateLogicalDevice();                       // 创建逻辑设备
    bool CreateSwapchain();                           // 创建交换链
    bool RecreateSwapchain();                         // 重建交换链（窗口大小变化时）
    bool CreateImageViews();                          // 创建图像视图
    bool CreateRenderPass();                          // 创建渲染通道
    bool CreatePipelineForTopology(DrawTopology topology);  // 创建指定拓扑的管线
    bool CreateFramebuffers();                        // 创建帧缓冲区
    bool CreateCommandPool();                         // 创建命令池
    bool CreateCommandBuffers();                      // 创建命令缓冲区
    bool CreateSyncObjects();                         // 创建同步对象（信号量、围栏）

    void CleanupSwapchain();                          // 清理交换链相关资源

    // 设备和交换链选择
    VulkanQueueFamilyIndices FindQueueFamilies(VkPhysicalDevice device);  // 查找队列族
    VulkanSwapchainSupportDetails QuerySwapchainSupport(VkPhysicalDevice device);  // 查询交换链支持
    bool IsDeviceSuitable(VkPhysicalDevice device);   // 设备是否适合
    int RateDevice(VkPhysicalDevice device);          // 设备评分

    // 交换链选择
    VkSurfaceFormatKHR ChooseSwapSurfaceFormat(const std::vector<VkSurfaceFormatKHR>& availableFormats);
    VkPresentModeKHR ChooseSwapPresentMode(const std::vector<VkPresentModeKHR>& availablePresentModes);
    VkExtent2D ChooseSwapExtent(const VkSurfaceCapabilitiesKHR& capabilities);

    // 内存类型查找
    uint32_t FindMemoryType(uint32_t typeFilter, VkMemoryPropertyFlags properties);

    VkResult CreateShaderModule(const std::vector<char>& code, VkShaderModule* shaderModule);

    std::vector<char> ReadShaderFile(const std::string& filename);
    VkShaderModule CreateShaderModuleHelper(const std::vector<char>& code);

    // 描述符集相关
    bool CreateDescriptorSetLayout();                   // 创建描述符集布局
    bool CreateUniformBuffers();                        // 创建统一缓冲区
    bool CreateDescriptorPool();                        // 创建描述符池
    bool CreateDescriptorSets();                        // 创建描述符集
    void DestroyUniformBuffers();                       // 销毁统一缓冲区
    void InitIdentityMatrix(Mat4& mat);                 // 初始化为单位矩阵

    // 调试回调函数
    static VKAPI_ATTR VkBool32 VKAPI_CALL DebugCallback(
        VkDebugUtilsMessageSeverityFlagBitsEXT severity,
        VkDebugUtilsMessageTypeFlagsEXT type,
        const VkDebugUtilsMessengerCallbackDataEXT* pCallbackData,
        void* pUserData);

protected:
    bool m_initialized = false;          // 是否已初始化
    uint32_t m_framebufferWidth = 800;   // 帧缓冲区宽度
    uint32_t m_framebufferHeight = 600;  // 帧缓冲区高度

    Vec4 m_clearColor = { 0.0f, 0.0f, 0.0f, 1.0f };  // 清除颜色（黑色）
    Rect2D m_viewport = { 0, 0, 800, 600 };            // 视口区域

    // Vulkan核心对象
    VkInstance m_instance = VK_NULL_HANDLE;             // Vulkan实例
    VkDebugUtilsMessengerEXT m_debugMessenger = VK_NULL_HANDLE;  // 调试消息回调
    VkSurfaceKHR m_surface = VK_NULL_HANDLE;            // 窗口表面
    VkPhysicalDevice m_physicalDevice = VK_NULL_HANDLE; // 物理设备
    VkDevice m_device = VK_NULL_HANDLE;                 // 逻辑设备

    VkQueue m_graphicsQueue = VK_NULL_HANDLE;  // 图形队列
    VkQueue m_presentQueue = VK_NULL_HANDLE;   // 呈现队列

    // 交换链相关
    VkSwapchainKHR m_swapchain = VK_NULL_HANDLE;        // 交换链
    std::vector<VkImage> m_swapchainImages;             // 交换链图像
    VkFormat m_swapchainImageFormat;                    // 交换链图像格式
    VkExtent2D m_swapchainExtent;                       // 交换链 extent
    std::vector<VkImageView> m_swapchainImageViews;     // 交换链图像视图
    std::vector<VkFramebuffer> m_swapchainFramebuffers; // 交换链帧缓冲区

    // 管线相关
    VkRenderPass m_renderPass = VK_NULL_HANDLE;                // 渲染通道
    VkPipelineLayout m_pipelineLayout = VK_NULL_HANDLE;
    VkPipeline m_pipelines[DT_COUNT] = {};                     // 多拓扑管线数组
    VkPipeline m_graphicsPipeline = VK_NULL_HANDLE;            // 图形管线（兼容旧代码）

    // 命令缓冲区
    VkCommandPool m_commandPool = VK_NULL_HANDLE;             // 命令池（渲染帧）
    VkCommandPool m_singleTimeCommandPool = VK_NULL_HANDLE;   // 单次命令专用池
    std::vector<VkCommandBuffer> m_commandBuffers;            // 命令缓冲区数组

    // 缓冲区内存映射（追踪 CreateBuffer 分配的内存）
    std::unordered_map<VkBuffer, VkDeviceMemory> m_bufferMemoryMap;

    // 描述符集相关
    VkDescriptorSetLayout m_descriptorSetLayout = VK_NULL_HANDLE;      // 描述符集布局
    VkDescriptorPool m_descriptorPool = VK_NULL_HANDLE;                // 描述符池
    std::vector<VkDescriptorSet> m_descriptorSets;                     // 描述符集（每帧一个）
    std::vector<VkBuffer> m_uniformBuffers;                            // 统一缓冲区（每帧一个）
    std::vector<VkDeviceMemory> m_uniformBuffersMemory;                // 统一缓冲区内存（每帧一个）
    std::vector<void*> m_uniformBuffersMapped;                         // 统一缓冲区映射指针

    // 同步对象
    std::vector<VkSemaphore> m_imageAvailableSemaphores;  // 图像可用信号量
    std::vector<VkSemaphore> m_renderFinishedSemaphores;  // 渲染完成信号量
    std::vector<VkFence> m_inFlightFences;                // 飞行中围栏
    uint32_t m_currentFrame = 0;                          // 当前帧索引
    uint32_t m_imageIndex = 0;                            // 当前交换链图像索引
    static const int MAX_FRAMES_IN_FLIGHT = 2;           // 最大飞行帧数（双缓冲）

    bool m_framebufferResized = false;  // 帧缓冲区是否已调整大小
    bool m_externalInstance = false;    // Instance 由外部管理（Qt 窗口）
    std::atomic<bool> m_shuttingDown{false};  // 关闭中标志（禁止异步回调创建新任务）

    // 验证层和设备扩展
    std::vector<const char*> m_validationLayers = {
        "VK_LAYER_KHRONOS_validation"  // Khronos验证层
    };

    std::vector<const char*> m_deviceExtensions = {
        VK_KHR_SWAPCHAIN_EXTENSION_NAME  // 交换链扩展
    };

#ifdef NDEBUG
    const bool m_enableValidationLayers = false;  // Release模式禁用验证层
#else
    const bool m_enableValidationLayers = true;   // Debug模式启用验证层
#endif
};

#endif //__VULKAN_RENDER_H__
