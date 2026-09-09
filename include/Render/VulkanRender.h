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

#ifndef __ENGINE_VEC_TYPES_H__
#define __ENGINE_VEC_TYPES_H__
struct Vec2 { float x, y; };
struct Vec3 { float x, y, z; };
struct Vec4 { float x, y, z, w; };
struct Mat4 { float m[4][4]; };
struct Rect2D { int x, y, width, height; };
#endif

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

class VulkanRender {
public:
    VulkanRender();
    virtual ~VulkanRender();

    VulkanRender(const VulkanRender&) = delete;
    VulkanRender& operator=(const VulkanRender&) = delete;

    enum DrawTopology {
        DT_TRIANGLE = 0,  // 三角形列表（多边形）
        DT_TRIANGLE_WIREFRAME, // 三角形线框
        DT_LINE,          // 线段列表
        DT_POINT,         // 点列表
        DT_COUNT
    };

    virtual bool IsWireframeEnabled() const { return false; }

    bool Initialize(const char* appName, uint32_t width, uint32_t height);
    virtual void Shutdown();
    bool IsInitialized() const;

    void Quiesce();

    void SetClearColor(float r, float g, float b, float a);

    virtual bool BeginFrame();                                    // 开始帧渲染，获取交换链图像
    virtual void EndFrame();                                      // 结束帧渲染，提交命令缓冲区
    virtual void DrawIndexed(uint32_t indexCount, uint32_t instanceCount = 1);  // 索引绘制

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

    void WaitForIdle();

    void SubmitAsync(QRunnable* task);

    bool IsShuttingDown() const;

    VkPipeline GetPipeline(DrawTopology topology) const;

    void SetSurface(VkSurfaceKHR surface);
    void SetInstance(VkInstance instance);
    void SetFramebufferSize(uint32_t width, uint32_t height);
    void SetFramebufferResized(bool resized);

    virtual void OnMouseDown(float nx, float ny, int button);
    virtual void OnMouseMove(float nx, float ny);
    virtual void OnMouseUp(int button);
    virtual void OnMouseWheel(float delta);

    static bool CheckValidationLayerSupport();        // 检查验证层支持
    static std::vector<const char*> GetRequiredExtensions();  // 获取所需扩展

protected:
    virtual bool OnInitialize() { return true; }
    virtual void OnShutdown() {}

    virtual void OnBeginFrame() {}
    virtual void OnEndFrame() {}

    virtual void OnRecreateSwapchain() {}

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
    bool m_initialized = false;          // 是否已初始化
    uint32_t m_framebufferWidth = 800;   // 帧缓冲区宽度
    uint32_t m_framebufferHeight = 600;  // 帧缓冲区高度

    Vec4 m_clearColor = { 0.1f, 0.1f, 0.12f, 1.0f };

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

    bool m_framebufferResized = false;  // 帧缓冲区是否已调整大小
    bool m_externalInstance = false;    // 实例由外部管理（Qt 窗口）
    std::atomic<bool> m_shuttingDown{false};  // 关闭中标志（禁止异步回调创建新任务）

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

#endif //__VULKAN_RENDER_H__
