#include "VulkanRender.h"
#include <iostream>
#include <fstream>
#include <set>
#include <algorithm>
#include <stdexcept>
#include <cstring>
#include <QRunnable>
#include <QThreadPool>
#include <QCoreApplication>

// 错误处理宏
#define VK_CHECK_RESULT(result, msg) \
    if (result != VK_SUCCESS) { \
        std::cerr << "Vulkan错误: " << msg << " (错误码: " << result << ")" << std::endl; \
        return false; \
    }

bool VulkanQueueFamilyIndices::IsComplete() const { return graphicsFamily >= 0 && presentFamily >= 0; }

// ============================================================================
// VulkanRender 构造和析构
// ============================================================================

VulkanRender::VulkanRender() {
    m_clearValues[0].color = { 0.1f, 0.1f, 0.12f, 1.0f };
    m_clearValueCount = 1;
}

VulkanRender::~VulkanRender() {
    Shutdown();
}

// ============================================================================
// 初始化和关闭
// ============================================================================

// 初始化Vulkan渲染器
bool VulkanRender::Initialize(const char* appName, uint32_t width, uint32_t height) {
    m_framebufferWidth = width;
    m_framebufferHeight = height;

    // 按顺序创建Vulkan对象
    // 如果外部已创建 Instance（Qt 窗口），跳过创建
    if (m_instance == VK_NULL_HANDLE) {
        if (!CreateInstance(appName)) return false;
        m_externalInstance = false;
    }
    if (m_enableValidationLayers && m_debugMessenger == VK_NULL_HANDLE) {
        if (!SetupDebugMessenger()) return false;
    }

    if (!PickPhysicalDevice()) return false;
    if (!CreateLogicalDevice()) return false;
    if (!CreateSwapchain()) return false;
    if (!CreateImageViews()) return false;

    // [虚钩子] 派生类初始化（描述符集布局/池/统一缓冲等）
    if (!OnInitialize()) return false;

    // [虚] 创建渲染通道
    if (!CreateRenderPass()) return false;

    // [虚] 创建管线（派生类加载着色器并创建管线）
    if (!CreatePipelines()) return false;

    // [虚] 创建帧缓冲
    if (!CreateFramebuffers()) return false;

    if (!CreateCommandPool()) return false;
    if (!CreateCommandBuffers()) return false;
    if (!CreateSyncObjects()) return false;

    m_initialized = true;
    return true;
}

// 停止异步任务并等待完成，但不销毁任何 Vulkan 资源
void VulkanRender::Quiesce() {
    if (!m_initialized) return;

    // 设置关闭标志，阻止异步回调创建新的任务
    m_shuttingDown = true;

    // 等待所有已提交的 QRunnable 执行完毕
    QThreadPool::globalInstance()->waitForDone();
    // 处理 QRunnable 回调中投递的 QueuedConnection 事件
    // （回调中会检查 m_shuttingDown，不会创建新任务）
    QCoreApplication::processEvents(QEventLoop::ExcludeUserInputEvents);

    // 等待设备空闲
    vkDeviceWaitIdle(m_device);
}

// 关闭并清理所有Vulkan资源
void VulkanRender::Shutdown() {
    if (!m_initialized) return;

    // 先停止异步活动（幂等）
    Quiesce();

    // 清理交换链
    CleanupSwapchain();

    // 销毁同步对象
    for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
        vkDestroySemaphore(m_device, m_imageAvailableSemaphores[i], nullptr);
        vkDestroySemaphore(m_device, m_renderFinishedSemaphores[i], nullptr);
        vkDestroyFence(m_device, m_inFlightFences[i], nullptr);
    }

    // 销毁命令池
    if (m_singleTimeCommandPool != VK_NULL_HANDLE) {
        vkDestroyCommandPool(m_device, m_singleTimeCommandPool, nullptr);
        m_singleTimeCommandPool = VK_NULL_HANDLE;
    }
    vkDestroyCommandPool(m_device, m_commandPool, nullptr);

    // 销毁图形管线
    for (int i = 0; i < DT_COUNT; i++) {
        if (m_pipelines[i] != VK_NULL_HANDLE) {
            vkDestroyPipeline(m_device, m_pipelines[i], nullptr);
            m_pipelines[i] = VK_NULL_HANDLE;
        }
    }
    if (m_pipelineLayout != VK_NULL_HANDLE) {
        vkDestroyPipelineLayout(m_device, m_pipelineLayout, nullptr);
        m_pipelineLayout = VK_NULL_HANDLE;
    }
    if (m_renderPass != VK_NULL_HANDLE) {
        vkDestroyRenderPass(m_device, m_renderPass, nullptr);
        m_renderPass = VK_NULL_HANDLE;
    }

    // [虚钩子] 派生类清理（描述符集、UBO 等）
    OnShutdown();

    // 销毁逻辑设备
    vkDestroyDevice(m_device, nullptr);

    // 销毁调试回调
    if (m_enableValidationLayers) {
        auto func = (PFN_vkDestroyDebugUtilsMessengerEXT)vkGetInstanceProcAddr(m_instance, "vkDestroyDebugUtilsMessengerEXT");
        if (func) func(m_instance, m_debugMessenger, nullptr);
    }

    // 销毁实例（仅当由内部创建时）
    if (!m_externalInstance) {
        vkDestroyInstance(m_instance, nullptr);
    }

    m_initialized = false;
}



void VulkanRender::SetClearColor(float r, float g, float b, float a) {
    m_clearColor = { r, g, b, a };
}

// ============================================================================
// 帧渲染控制
// ============================================================================

// 开始帧渲染
bool VulkanRender::BeginFrame() {
    // 等待当前帧的围栏
    vkWaitForFences(m_device, 1, &m_inFlightFences[m_currentFrame], VK_TRUE, UINT64_MAX);

    // 获取交换链图像索引
    uint32_t imageIndex;
    VkResult result = vkAcquireNextImageKHR(
        m_device, m_swapchain, UINT64_MAX,
        m_imageAvailableSemaphores[m_currentFrame], VK_NULL_HANDLE, &imageIndex);

    // 如果交换链过时，重建后重试
    if (result == VK_ERROR_OUT_OF_DATE_KHR) {
        RecreateSwapchain();
        return false;
    } else if (result != VK_SUCCESS && result != VK_SUBOPTIMAL_KHR) {
        throw std::runtime_error("获取交换链图像失败");
    }

    m_imageIndex = imageIndex;

    vkResetFences(m_device, 1, &m_inFlightFences[m_currentFrame]);

    // 重置命令缓冲区并开始录制
    vkResetCommandBuffer(m_commandBuffers[m_currentFrame], 0);
    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    vkBeginCommandBuffer(m_commandBuffers[m_currentFrame], &beginInfo);

    // [虚钩子] 派生类在渲染通道开始前的操作（更新UBO、绑定描述符集等）
    OnBeginFrame();

    // 开始渲染通道
    VkRenderPassBeginInfo renderPassInfo{};
    renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    renderPassInfo.renderPass = m_renderPass;
    renderPassInfo.framebuffer = m_swapchainFramebuffers[imageIndex];
    renderPassInfo.renderArea.offset = { 0, 0 };
    renderPassInfo.renderArea.extent = m_swapchainExtent;

    // 使用派生类配置的清除值（基类默认 1 个颜色清除值，3D 派生类设为 2 个）
    renderPassInfo.clearValueCount = m_clearValueCount;
    renderPassInfo.pClearValues = m_clearValues;

    vkCmdBeginRenderPass(m_commandBuffers[m_currentFrame], &renderPassInfo, VK_SUBPASS_CONTENTS_INLINE);

    // 设置视口（匹配交换链尺寸，确保渲染铺满整个窗口）
    VkViewport viewport{};
    viewport.x = 0.0f;
    viewport.y = 0.0f;
    viewport.width = static_cast<float>(m_swapchainExtent.width);
    viewport.height = static_cast<float>(m_swapchainExtent.height);
    viewport.minDepth = 0.0f;
    viewport.maxDepth = 1.0f;
    vkCmdSetViewport(m_commandBuffers[m_currentFrame], 0, 1, &viewport);

    // 设置裁剪矩形
    VkRect2D scissor{};
    scissor.offset = { 0, 0 };
    scissor.extent = m_swapchainExtent;
    vkCmdSetScissor(m_commandBuffers[m_currentFrame], 0, 1, &scissor);

    return true;
}

// 结束帧渲染
void VulkanRender::EndFrame() {
    // 结束渲染通道
    vkCmdEndRenderPass(m_commandBuffers[m_currentFrame]);

    // [虚钩子] 派生类在渲染通道结束后的操作
    OnEndFrame();

    // 结束命令缓冲区录制
    vkEndCommandBuffer(m_commandBuffers[m_currentFrame]);

    // 提交命令缓冲区
    VkSubmitInfo submitInfo{};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;

    // 等待图像可用信号量
    VkSemaphore waitSemaphores[] = { m_imageAvailableSemaphores[m_currentFrame] };
    VkPipelineStageFlags waitStages[] = { VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT };
    submitInfo.waitSemaphoreCount = 1;
    submitInfo.pWaitSemaphores = waitSemaphores;
    submitInfo.pWaitDstStageMask = waitStages;
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &m_commandBuffers[m_currentFrame];

    // 信号渲染完成信号量
    VkSemaphore signalSemaphores[] = { m_renderFinishedSemaphores[m_currentFrame] };
    submitInfo.signalSemaphoreCount = 1;
    submitInfo.pSignalSemaphores = signalSemaphores;

    // 提交命令缓冲区
    if (vkQueueSubmit(m_graphicsQueue, 1, &submitInfo, m_inFlightFences[m_currentFrame]) != VK_SUCCESS) {
        throw std::runtime_error("提交命令缓冲区失败");
    }

    // 呈现图像
    VkPresentInfoKHR presentInfo{};
    presentInfo.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
    presentInfo.waitSemaphoreCount = 1;
    presentInfo.pWaitSemaphores = signalSemaphores;

    VkSwapchainKHR swapChains[] = { m_swapchain };
    presentInfo.swapchainCount = 1;
    presentInfo.pSwapchains = swapChains;
    presentInfo.pImageIndices = &m_imageIndex;

    VkResult result = vkQueuePresentKHR(m_presentQueue, &presentInfo);

    // 如果交换链过时或次优，重建
    if (result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_SUBOPTIMAL_KHR || m_framebufferResized) {
        m_framebufferResized = false;
        RecreateSwapchain();
    } else if (result != VK_SUCCESS) {
        throw std::runtime_error("呈现图像失败");
    }

    // 更新当前帧索引
    m_currentFrame = (m_currentFrame + 1) % MAX_FRAMES_IN_FLIGHT;
}

// 索引绘制
void VulkanRender::DrawIndexed(uint32_t indexCount, uint32_t instanceCount) {
    vkCmdDrawIndexed(m_commandBuffers[m_currentFrame], indexCount, instanceCount, 0, 0, 0);
}

// ============================================================================
// 缓冲区操作
// ============================================================================

// 创建缓冲区
VkBuffer VulkanRender::CreateBuffer(VkDeviceSize size, VkBufferUsageFlags usage, VkMemoryPropertyFlags properties) {
    VkBuffer buffer;
    VkBufferCreateInfo bufferInfo{};
    bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufferInfo.size = size;
    bufferInfo.usage = usage;
    bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    if (vkCreateBuffer(m_device, &bufferInfo, nullptr, &buffer) != VK_SUCCESS) {
        throw std::runtime_error("创建缓冲区失败");
    }

    // 分配缓冲区内存
    VkMemoryRequirements memRequirements;
    vkGetBufferMemoryRequirements(m_device, buffer, &memRequirements);

    VkMemoryAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocInfo.allocationSize = memRequirements.size;
    allocInfo.memoryTypeIndex = FindMemoryType(memRequirements.memoryTypeBits, properties);

    VkDeviceMemory bufferMemory;
    if (vkAllocateMemory(m_device, &allocInfo, nullptr, &bufferMemory) != VK_SUCCESS) {
        vkDestroyBuffer(m_device, buffer, nullptr);
        throw std::runtime_error("分配缓冲区内存失败");
    }

    // 绑定内存到缓冲区
    vkBindBufferMemory(m_device, buffer, bufferMemory, 0);

    m_bufferMemoryMap[buffer] = bufferMemory;
    return buffer;
}

// 销毁缓冲区
void VulkanRender::DestroyBuffer(VkBuffer buffer) {
    if (buffer == VK_NULL_HANDLE) return;

    auto it = m_bufferMemoryMap.find(buffer);
    if (it != m_bufferMemoryMap.end()) {
        vkFreeMemory(m_device, it->second, nullptr);
        m_bufferMemoryMap.erase(it);
    }
    vkDestroyBuffer(m_device, buffer, nullptr);
}

// 映射缓冲区内存
VkResult VulkanRender::MapBuffer(VkBuffer buffer, void** data) {
    auto it = m_bufferMemoryMap.find(buffer);
    if (it == m_bufferMemoryMap.end()) {
        return VK_ERROR_MEMORY_MAP_FAILED;
    }
    return vkMapMemory(m_device, it->second, 0, VK_WHOLE_SIZE, 0, data);
}

// 取消映射
void VulkanRender::UnmapBuffer(VkBuffer buffer) {
    auto it = m_bufferMemoryMap.find(buffer);
    if (it != m_bufferMemoryMap.end()) {
        vkUnmapMemory(m_device, it->second);
    }
}

// 分配设备内存
VkDeviceMemory VulkanRender::AllocateMemory(VkMemoryRequirements memRequirements, VkMemoryPropertyFlags properties) {
    VkMemoryAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocInfo.allocationSize = memRequirements.size;
    allocInfo.memoryTypeIndex = FindMemoryType(memRequirements.memoryTypeBits, properties);

    VkDeviceMemory memory;
    if (vkAllocateMemory(m_device, &allocInfo, nullptr, &memory) != VK_SUCCESS) {
        throw std::runtime_error("分配设备内存失败");
    }

    return memory;
}

// ============================================================================
// 单次命令缓冲区
// ============================================================================

// 开始单次命令
VkCommandBuffer VulkanRender::BeginSingleTimeCommands() {
    VkCommandBufferAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocInfo.commandPool = m_singleTimeCommandPool;
    allocInfo.commandBufferCount = 1;

    VkCommandBuffer commandBuffer;
    vkAllocateCommandBuffers(m_device, &allocInfo, &commandBuffer);

    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

    vkBeginCommandBuffer(commandBuffer, &beginInfo);
    return commandBuffer;
}

// 结束单次命令
void VulkanRender::EndSingleTimeCommands(VkCommandBuffer commandBuffer) {
    vkEndCommandBuffer(commandBuffer);

    VkSubmitInfo submitInfo{};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &commandBuffer;

    vkQueueSubmit(m_graphicsQueue, 1, &submitInfo, VK_NULL_HANDLE);
    vkQueueWaitIdle(m_graphicsQueue);

    vkFreeCommandBuffers(m_device, m_singleTimeCommandPool, 1, &commandBuffer);
}

// 等待设备空闲
void VulkanRender::WaitForIdle() {
    vkDeviceWaitIdle(m_device);
}

// ============================================================================
// 静态工具函数
// ============================================================================

// 检查验证层支持
bool VulkanRender::CheckValidationLayerSupport() {
    uint32_t layerCount;
    vkEnumerateInstanceLayerProperties(&layerCount, nullptr);

    std::vector<VkLayerProperties> availableLayers(layerCount);
    vkEnumerateInstanceLayerProperties(&layerCount, availableLayers.data());

    for (const char* layerName : {"VK_LAYER_KHRONOS_validation"}) {
        bool layerFound = false;
        for (const auto& layerProperties : availableLayers) {
            if (strcmp(layerName, layerProperties.layerName) == 0) {
                layerFound = true;
                break;
            }
        }
        if (!layerFound) return false;
    }
    return true;
}

// 获取所需扩展
std::vector<const char*> VulkanRender::GetRequiredExtensions() {
    std::vector<const char*> extensions;
    extensions.push_back(VK_KHR_SURFACE_EXTENSION_NAME);
    extensions.push_back("VK_KHR_win32_surface");

    // 如果启用验证层，添加调试扩展
#ifndef NDEBUG
    extensions.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
#endif

    return extensions;
}

// ============================================================================
// Vulkan对象创建（受保护）
// ============================================================================

// 创建Vulkan实例
bool VulkanRender::CreateInstance(const char* appName) {
    // 检查验证层支持
    if (m_enableValidationLayers && !CheckValidationLayerSupport()) {
        std::cerr << "验证层不可用" << std::endl;
        return false;
    }

    // 应用信息
    VkApplicationInfo appInfo{};
    appInfo.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    appInfo.pApplicationName = appName;
    appInfo.applicationVersion = VK_MAKE_VERSION(1, 0, 0);
    appInfo.pEngineName = "VulkanEngine";
    appInfo.engineVersion = VK_MAKE_VERSION(1, 0, 0);
    appInfo.apiVersion = VK_API_VERSION_1_0;

    // 实例创建信息
    VkInstanceCreateInfo createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    createInfo.pApplicationInfo = &appInfo;

    // 获取所需扩展
    auto extensions = GetRequiredExtensions();
    createInfo.enabledExtensionCount = static_cast<uint32_t>(extensions.size());
    createInfo.ppEnabledExtensionNames = extensions.data();

    // 设置验证层
    VkDebugUtilsMessengerCreateInfoEXT debugCreateInfo{};
    if (m_enableValidationLayers) {
        createInfo.enabledLayerCount = static_cast<uint32_t>(m_validationLayers.size());
        createInfo.ppEnabledLayerNames = m_validationLayers.data();

        // 设置调试回调
        debugCreateInfo.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT;
        debugCreateInfo.messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT |
                                         VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT |
                                         VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
        debugCreateInfo.messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT |
                                     VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT |
                                     VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
        debugCreateInfo.pfnUserCallback = DebugCallback;

        createInfo.pNext = &debugCreateInfo;
    } else {
        createInfo.enabledLayerCount = 0;
        createInfo.pNext = nullptr;
    }

    // 创建实例
    VkResult result = vkCreateInstance(&createInfo, nullptr, &m_instance);
    if (result != VK_SUCCESS) {
        std::cerr << "创建Vulkan实例失败" << std::endl;
        return false;
    }

    return true;
}

// 设置调试消息回调
bool VulkanRender::SetupDebugMessenger() {
    VkDebugUtilsMessengerCreateInfoEXT createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT;
    createInfo.messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT |
                                 VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT |
                                 VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
    createInfo.messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT |
                             VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT |
                             VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
    createInfo.pfnUserCallback = DebugCallback;
    createInfo.pUserData = nullptr;

    auto func = (PFN_vkCreateDebugUtilsMessengerEXT)vkGetInstanceProcAddr(m_instance, "vkCreateDebugUtilsMessengerEXT");
    if (func) {
        VkResult result = func(m_instance, &createInfo, nullptr, &m_debugMessenger);
        if (result != VK_SUCCESS) {
            std::cerr << "设置调试回调失败" << std::endl;
            return false;
        }
    }

    return true;
}

// 选择物理设备
bool VulkanRender::PickPhysicalDevice() {
    uint32_t deviceCount = 0;
    vkEnumeratePhysicalDevices(m_instance, &deviceCount, nullptr);

    if (deviceCount == 0) {
        std::cerr << "未找到支持Vulkan的GPU" << std::endl;
        return false;
    }

    std::vector<VkPhysicalDevice> devices(deviceCount);
    vkEnumeratePhysicalDevices(m_instance, &deviceCount, devices.data());

    // 选择评分最高的设备
    int bestScore = -1;
    for (const auto& device : devices) {
        int score = RateDevice(device);
        if (score > bestScore && IsDeviceSuitable(device)) {
            bestScore = score;
            m_physicalDevice = device;
        }
    }

    if (m_physicalDevice == VK_NULL_HANDLE) {
        std::cerr << "未找到合适的GPU" << std::endl;
        return false;
    }

    return true;
}

// 创建逻辑设备
bool VulkanRender::CreateLogicalDevice() {
    // 查找队列族
    VulkanQueueFamilyIndices indices = FindQueueFamilies(m_physicalDevice);

    // 创建逻辑设备需要的队列信息
    std::vector<VkDeviceQueueCreateInfo> queueCreateInfos;
    std::set<int> uniqueQueueFamilies = { indices.graphicsFamily, indices.presentFamily };

    float queuePriority = 1.0f;
    for (int queueFamily : uniqueQueueFamilies) {
        VkDeviceQueueCreateInfo queueCreateInfo{};
        queueCreateInfo.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
        queueCreateInfo.queueFamilyIndex = queueFamily;
        queueCreateInfo.queueCount = 1;
        queueCreateInfo.pQueuePriorities = &queuePriority;
        queueCreateInfos.push_back(queueCreateInfo);
    }

    // 设备特性
    VkPhysicalDeviceFeatures deviceFeatures{};
    deviceFeatures.samplerAnisotropy = VK_TRUE;  // 启用各向异性过滤
    deviceFeatures.fillModeNonSolid = VK_TRUE;   // 启用线框模式

    // 创建逻辑设备
    VkDeviceCreateInfo createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
    createInfo.queueCreateInfoCount = static_cast<uint32_t>(queueCreateInfos.size());
    createInfo.pQueueCreateInfos = queueCreateInfos.data();
    createInfo.pEnabledFeatures = &deviceFeatures;
    createInfo.enabledExtensionCount = static_cast<uint32_t>(m_deviceExtensions.size());
    createInfo.ppEnabledExtensionNames = m_deviceExtensions.data();

    VkResult result = vkCreateDevice(m_physicalDevice, &createInfo, nullptr, &m_device);
    if (result != VK_SUCCESS) {
        std::cerr << "创建逻辑设备失败" << std::endl;
        return false;
    }

    // 获取队列句柄
    vkGetDeviceQueue(m_device, indices.graphicsFamily, 0, &m_graphicsQueue);
    vkGetDeviceQueue(m_device, indices.presentFamily, 0, &m_presentQueue);

    return true;
}

// 创建交换链
bool VulkanRender::CreateSwapchain() {
    VulkanSwapchainSupportDetails swapChainSupport = QuerySwapchainSupport(m_physicalDevice);

    VkSurfaceFormatKHR surfaceFormat = ChooseSwapSurfaceFormat(swapChainSupport.formats);
    VkPresentModeKHR presentMode = ChooseSwapPresentMode(swapChainSupport.presentModes);
    VkExtent2D extent = ChooseSwapExtent(swapChainSupport.capabilities);

    // 选择图像数量（至少比最小值多1）
    uint32_t imageCount = swapChainSupport.capabilities.minImageCount + 1;
    if (swapChainSupport.capabilities.maxImageCount > 0 && imageCount > swapChainSupport.capabilities.maxImageCount) {
        imageCount = swapChainSupport.capabilities.maxImageCount;
    }

    // 创建交换链
    VkSwapchainCreateInfoKHR createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
    createInfo.surface = m_surface;
    createInfo.minImageCount = imageCount;
    createInfo.imageFormat = surfaceFormat.format;
    createInfo.imageColorSpace = surfaceFormat.colorSpace;
    createInfo.imageExtent = extent;
    createInfo.imageArrayLayers = 1;
    createInfo.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;

    // 设置队列族
    VulkanQueueFamilyIndices indices = FindQueueFamilies(m_physicalDevice);
    uint32_t queueFamilyIndices[] = { static_cast<uint32_t>(indices.graphicsFamily),
                                       static_cast<uint32_t>(indices.presentFamily) };

    if (indices.graphicsFamily != indices.presentFamily) {
        createInfo.imageSharingMode = VK_SHARING_MODE_CONCURRENT;
        createInfo.queueFamilyIndexCount = 2;
        createInfo.pQueueFamilyIndices = queueFamilyIndices;
    } else {
        createInfo.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
    }

    createInfo.preTransform = swapChainSupport.capabilities.currentTransform;
    createInfo.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
    createInfo.presentMode = presentMode;
    createInfo.clipped = VK_TRUE;
    createInfo.oldSwapchain = VK_NULL_HANDLE;

    VkResult result = vkCreateSwapchainKHR(m_device, &createInfo, nullptr, &m_swapchain);
    if (result != VK_SUCCESS) {
        std::cerr << "创建交换链失败" << std::endl;
        return false;
    }

    // 获取交换链图像
    vkGetSwapchainImagesKHR(m_device, m_swapchain, &imageCount, nullptr);
    m_swapchainImages.resize(imageCount);
    vkGetSwapchainImagesKHR(m_device, m_swapchain, &imageCount, m_swapchainImages.data());

    m_swapchainImageFormat = surfaceFormat.format;
    m_swapchainExtent = extent;

    return true;
}

// 重建交换链
bool VulkanRender::RecreateSwapchain() {
    // 等待设备空闲
    vkDeviceWaitIdle(m_device);

    // 清理旧交换链
    CleanupSwapchain();

    // [虚钩子] 派生类清理自己的资源（如深度缓冲）
    OnRecreateSwapchain();

    // 重新创建
    if (!CreateSwapchain()) return false;
    if (!CreateImageViews()) return false;
    if (!CreateFramebuffers()) return false;

    return true;
}

// 创建图像视图
bool VulkanRender::CreateImageViews() {
    m_swapchainImageViews.resize(m_swapchainImages.size());

    for (size_t i = 0; i < m_swapchainImages.size(); i++) {
        VkImageViewCreateInfo createInfo{};
        createInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        createInfo.image = m_swapchainImages[i];
        createInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
        createInfo.format = m_swapchainImageFormat;
        createInfo.components.r = VK_COMPONENT_SWIZZLE_IDENTITY;
        createInfo.components.g = VK_COMPONENT_SWIZZLE_IDENTITY;
        createInfo.components.b = VK_COMPONENT_SWIZZLE_IDENTITY;
        createInfo.components.a = VK_COMPONENT_SWIZZLE_IDENTITY;
        createInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        createInfo.subresourceRange.baseMipLevel = 0;
        createInfo.subresourceRange.levelCount = 1;
        createInfo.subresourceRange.baseArrayLayer = 0;
        createInfo.subresourceRange.layerCount = 1;

        if (vkCreateImageView(m_device, &createInfo, nullptr, &m_swapchainImageViews[i]) != VK_SUCCESS) {
            std::cerr << "创建图像视图失败" << std::endl;
            return false;
        }
    }

    return true;
}

// 创建渲染通道（基类默认：纯颜色附件）
bool VulkanRender::CreateRenderPass() {
    VkAttachmentDescription colorAttachment{};
    colorAttachment.format = m_swapchainImageFormat;
    colorAttachment.samples = VK_SAMPLE_COUNT_1_BIT;
    colorAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;       // 渲染前清除
    colorAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;     // 渲染后存储
    colorAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    colorAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    colorAttachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    colorAttachment.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

    VkAttachmentReference colorAttachmentRef{};
    colorAttachmentRef.attachment = 0;
    colorAttachmentRef.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

    VkSubpassDescription subpass{};
    subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.colorAttachmentCount = 1;
    subpass.pColorAttachments = &colorAttachmentRef;

    // 子通道依赖（确保图像布局转换正确）
    VkSubpassDependency dependency{};
    dependency.srcSubpass = VK_SUBPASS_EXTERNAL;
    dependency.dstSubpass = 0;
    dependency.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    dependency.srcAccessMask = 0;
    dependency.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    dependency.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;

    // 创建渲染通道
    VkRenderPassCreateInfo renderPassInfo{};
    renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    renderPassInfo.attachmentCount = 1;
    renderPassInfo.pAttachments = &colorAttachment;
    renderPassInfo.subpassCount = 1;
    renderPassInfo.pSubpasses = &subpass;
    renderPassInfo.dependencyCount = 1;
    renderPassInfo.pDependencies = &dependency;

    if (vkCreateRenderPass(m_device, &renderPassInfo, nullptr, &m_renderPass) != VK_SUCCESS) {
        std::cerr << "创建渲染通道失败" << std::endl;
        return false;
    }

    return true;
}

// 创建管线（基类默认：空，派生类加载着色器并创建管线）
bool VulkanRender::CreatePipelines() {
    // 基类默认不创建管线，由派生类重写
    return true;
}

// 创建帧缓冲区（基类默认：纯颜色帧缓冲）
bool VulkanRender::CreateFramebuffers() {
    m_swapchainFramebuffers.resize(m_swapchainImageViews.size());

    for (size_t i = 0; i < m_swapchainImageViews.size(); i++) {
        VkImageView attachments[] = { m_swapchainImageViews[i] };

        VkFramebufferCreateInfo framebufferInfo{};
        framebufferInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
        framebufferInfo.renderPass = m_renderPass;
        framebufferInfo.attachmentCount = 1;
        framebufferInfo.pAttachments = attachments;
        framebufferInfo.width = m_swapchainExtent.width;
        framebufferInfo.height = m_swapchainExtent.height;
        framebufferInfo.layers = 1;

        if (vkCreateFramebuffer(m_device, &framebufferInfo, nullptr, &m_swapchainFramebuffers[i]) != VK_SUCCESS) {
            std::cerr << "创建帧缓冲区失败" << std::endl;
            return false;
        }
    }

    return true;
}

// 创建命令池
bool VulkanRender::CreateCommandPool() {
    VulkanQueueFamilyIndices queueFamilyIndices = FindQueueFamilies(m_physicalDevice);

    VkCommandPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    poolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    poolInfo.queueFamilyIndex = queueFamilyIndices.graphicsFamily;

    if (vkCreateCommandPool(m_device, &poolInfo, nullptr, &m_commandPool) != VK_SUCCESS) {
        std::cerr << "创建命令池失败" << std::endl;
        return false;
    }

    // 创建单次命令专用命令池（VK_COMMAND_POOL_CREATE_TRANSIENT_BIT 提示驱动优化）
    poolInfo.flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT;
    if (vkCreateCommandPool(m_device, &poolInfo, nullptr, &m_singleTimeCommandPool) != VK_SUCCESS) {
        std::cerr << "创建单次命令池失败" << std::endl;
        return false;
    }

    return true;
}

// 创建命令缓冲区
bool VulkanRender::CreateCommandBuffers() {
    m_commandBuffers.resize(MAX_FRAMES_IN_FLIGHT);

    VkCommandBufferAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    allocInfo.commandPool = m_commandPool;
    allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocInfo.commandBufferCount = static_cast<uint32_t>(m_commandBuffers.size());

    if (vkAllocateCommandBuffers(m_device, &allocInfo, m_commandBuffers.data()) != VK_SUCCESS) {
        std::cerr << "分配命令缓冲区失败" << std::endl;
        return false;
    }

    return true;
}

// 创建同步对象
bool VulkanRender::CreateSyncObjects() {
    m_imageAvailableSemaphores.resize(MAX_FRAMES_IN_FLIGHT);
    m_renderFinishedSemaphores.resize(MAX_FRAMES_IN_FLIGHT);
    m_inFlightFences.resize(MAX_FRAMES_IN_FLIGHT);

    VkSemaphoreCreateInfo semaphoreInfo{};
    semaphoreInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;

    VkFenceCreateInfo fenceInfo{};
    fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    fenceInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;  // 初始状态为已信号

    for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
        if (vkCreateSemaphore(m_device, &semaphoreInfo, nullptr, &m_imageAvailableSemaphores[i]) != VK_SUCCESS ||
            vkCreateSemaphore(m_device, &semaphoreInfo, nullptr, &m_renderFinishedSemaphores[i]) != VK_SUCCESS ||
            vkCreateFence(m_device, &fenceInfo, nullptr, &m_inFlightFences[i]) != VK_SUCCESS) {
            std::cerr << "创建同步对象失败" << std::endl;
            return false;
        }
    }

    return true;
}

// 清理交换链相关资源
void VulkanRender::CleanupSwapchain() {
    for (auto framebuffer : m_swapchainFramebuffers) {
        vkDestroyFramebuffer(m_device, framebuffer, nullptr);
    }
    m_swapchainFramebuffers.clear();

    for (auto imageView : m_swapchainImageViews) {
        vkDestroyImageView(m_device, imageView, nullptr);
    }
    m_swapchainImageViews.clear();

    vkDestroySwapchainKHR(m_device, m_swapchain, nullptr);
}

// ============================================================================
// 设备和交换链选择
// ============================================================================

// 查找队列族
VulkanQueueFamilyIndices VulkanRender::FindQueueFamilies(VkPhysicalDevice device) {
    VulkanQueueFamilyIndices indices;

    uint32_t queueFamilyCount = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(device, &queueFamilyCount, nullptr);

    std::vector<VkQueueFamilyProperties> queueFamilies(queueFamilyCount);
    vkGetPhysicalDeviceQueueFamilyProperties(device, &queueFamilyCount, queueFamilies.data());

    int i = 0;
    for (const auto& queueFamily : queueFamilies) {
        // 查找图形队列
        if (queueFamily.queueFlags & VK_QUEUE_GRAPHICS_BIT) {
            indices.graphicsFamily = i;
        }

        // 查找呈现队列
        VkBool32 presentSupport = false;
        vkGetPhysicalDeviceSurfaceSupportKHR(device, i, m_surface, &presentSupport);
        if (presentSupport) {
            indices.presentFamily = i;
        }

        if (indices.IsComplete()) break;
        i++;
    }

    return indices;
}

// 查询交换链支持
VulkanSwapchainSupportDetails VulkanRender::QuerySwapchainSupport(VkPhysicalDevice device) {
    VulkanSwapchainSupportDetails details;

    // 获取表面能力
    vkGetPhysicalDeviceSurfaceCapabilitiesKHR(device, m_surface, &details.capabilities);

    // 获取支持的格式
    uint32_t formatCount;
    vkGetPhysicalDeviceSurfaceFormatsKHR(device, m_surface, &formatCount, nullptr);
    if (formatCount != 0) {
        details.formats.resize(formatCount);
        vkGetPhysicalDeviceSurfaceFormatsKHR(device, m_surface, &formatCount, details.formats.data());
    }

    // 获取支持的呈现模式
    uint32_t presentModeCount;
    vkGetPhysicalDeviceSurfacePresentModesKHR(device, m_surface, &presentModeCount, nullptr);
    if (presentModeCount != 0) {
        details.presentModes.resize(presentModeCount);
        vkGetPhysicalDeviceSurfacePresentModesKHR(device, m_surface, &presentModeCount, details.presentModes.data());
    }

    return details;
}

// 设备是否适合
bool VulkanRender::IsDeviceSuitable(VkPhysicalDevice device) {
    VulkanQueueFamilyIndices indices = FindQueueFamilies(device);

    // 检查设备扩展支持
    uint32_t extensionCount;
    vkEnumerateDeviceExtensionProperties(device, nullptr, &extensionCount, nullptr);

    std::vector<VkExtensionProperties> availableExtensions(extensionCount);
    vkEnumerateDeviceExtensionProperties(device, nullptr, &extensionCount, availableExtensions.data());

    std::set<std::string> requiredExtensions(m_deviceExtensions.begin(), m_deviceExtensions.end());
    for (const auto& extension : availableExtensions) {
        requiredExtensions.erase(extension.extensionName);
    }

    // 检查交换链支持
    bool extensionsSupported = requiredExtensions.empty();
    bool swapChainAdequate = false;
    if (extensionsSupported) {
        VulkanSwapchainSupportDetails swapChainSupport = QuerySwapchainSupport(device);
        swapChainAdequate = !swapChainSupport.formats.empty() && !swapChainSupport.presentModes.empty();
    }

    return indices.IsComplete() && extensionsSupported && swapChainAdequate;
}

// 设备评分
int VulkanRender::RateDevice(VkPhysicalDevice device) {
    VkPhysicalDeviceProperties deviceProperties;
    vkGetPhysicalDeviceProperties(device, &deviceProperties);

    VkPhysicalDeviceFeatures deviceFeatures;
    vkGetPhysicalDeviceFeatures(device, &deviceFeatures);

    int score = 0;

    // 独显评分更高
    if (deviceProperties.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU) {
        score += 10000;
    }

    // 最大纹理尺寸
    score += deviceProperties.limits.maxImageDimension2D;

    // 需要几何着色器
    if (!deviceFeatures.geometryShader) {
        return 0;
    }

    return score;
}

// ============================================================================
// 交换链选择
// ============================================================================

// 选择表面格式
VkSurfaceFormatKHR VulkanRender::ChooseSwapSurfaceFormat(const std::vector<VkSurfaceFormatKHR>& availableFormats) {
    for (const auto& availableFormat : availableFormats) {
        // 优先选择B8G8R8A8_SRGB格式和SRGB非线性色彩空间
        if (availableFormat.format == VK_FORMAT_B8G8R8A8_SRGB &&
            availableFormat.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR) {
            return availableFormat;
        }
    }
    return availableFormats[0];
}

// 选择呈现模式
VkPresentModeKHR VulkanRender::ChooseSwapPresentMode(const std::vector<VkPresentModeKHR>& availablePresentModes) {
    // 优先选择Mailbox模式（无撕裂 + 低延迟）
    for (const auto& availablePresentMode : availablePresentModes) {
        if (availablePresentMode == VK_PRESENT_MODE_MAILBOX_KHR) {
            return availablePresentMode;
        }
    }
    return VK_PRESENT_MODE_FIFO_KHR;
}

// 选择交换链extent
VkExtent2D VulkanRender::ChooseSwapExtent(const VkSurfaceCapabilitiesKHR& capabilities) {
    if (capabilities.currentExtent.width != UINT32_MAX) {
        return capabilities.currentExtent;
    } else {
        int width, height;

        width = static_cast<int>(m_framebufferWidth);
        height = static_cast<int>(m_framebufferHeight);

        VkExtent2D actualExtent = {
            static_cast<uint32_t>(width),
            static_cast<uint32_t>(height)
        };

        actualExtent.width = std::clamp(actualExtent.width, capabilities.minImageExtent.width, capabilities.maxImageExtent.width);
        actualExtent.height = std::clamp(actualExtent.height, capabilities.minImageExtent.height, capabilities.maxImageExtent.height);

        return actualExtent;
    }
}

// ============================================================================
// 内存和着色器工具
// ============================================================================

// 查找内存类型
uint32_t VulkanRender::FindMemoryType(uint32_t typeFilter, VkMemoryPropertyFlags properties) {
    VkPhysicalDeviceMemoryProperties memProperties;
    vkGetPhysicalDeviceMemoryProperties(m_physicalDevice, &memProperties);

    for (uint32_t i = 0; i < memProperties.memoryTypeCount; i++) {
        if ((typeFilter & (1 << i)) && (memProperties.memoryTypes[i].propertyFlags & properties) == properties) {
            return i;
        }
    }

    throw std::runtime_error("未找到合适的内存类型");
}

// 创建着色器模块
VkResult VulkanRender::CreateShaderModule(const std::vector<char>& code, VkShaderModule* shaderModule) {
    VkShaderModuleCreateInfo createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    createInfo.codeSize = code.size();
    createInfo.pCode = reinterpret_cast<const uint32_t*>(code.data());

    return vkCreateShaderModule(m_device, &createInfo, nullptr, shaderModule);
}

// 调试回调函数
VKAPI_ATTR VkBool32 VKAPI_CALL VulkanRender::DebugCallback(
    VkDebugUtilsMessageSeverityFlagBitsEXT severity,
    VkDebugUtilsMessageTypeFlagsEXT type,
    const VkDebugUtilsMessengerCallbackDataEXT* pCallbackData,
    void* pUserData) {
    std::cerr << "验证层: " << pCallbackData->pMessage << std::endl;
    return VK_FALSE;
}

// ============================================================================
// 着色器文件加载辅助函数
// ============================================================================

// 从文件读取着色器字节码
std::vector<char> VulkanRender::ReadShaderFile(const std::string& filename) {
    std::ifstream file(filename, std::ios::ate | std::ios::binary);

    if (!file.is_open()) {
        throw std::runtime_error("无法打开着色器文件: " + filename);
    }

    size_t fileSize = static_cast<size_t>(file.tellg());
    std::vector<char> buffer(fileSize);

    file.seekg(0);
    file.read(buffer.data(), fileSize);
    file.close();

    return buffer;
}

// 创建着色器模块辅助函数
VkShaderModule VulkanRender::CreateShaderModuleHelper(const std::vector<char>& code) {
    VkShaderModule shaderModule;
    if (CreateShaderModule(code, &shaderModule) != VK_SUCCESS) {
        throw std::runtime_error("创建着色器模块失败");
    }
    return shaderModule;
}

void VulkanRender::SubmitAsync(QRunnable* task) {
    QThreadPool::globalInstance()->start(task);
}

bool VulkanRender::IsInitialized() const { return m_initialized; }

VkDevice VulkanRender::GetDevice() const { return m_device; }
VkPhysicalDevice VulkanRender::GetPhysicalDevice() const { return m_physicalDevice; }

VkCommandBuffer VulkanRender::GetCurrentCommandBuffer() const { return m_commandBuffers[m_currentFrame]; }


// 是否正在关闭（阻止关闭过程中产生的异步回调创建新任务）
bool VulkanRender::IsShuttingDown() const { return m_shuttingDown; }

VkPipeline VulkanRender::GetPipeline(DrawTopology topology) const {
    if (topology >= 0 && topology < DT_COUNT) {
        return m_pipelines[topology];
    }
    return VK_NULL_HANDLE;
}


// 窗口表面设置
void VulkanRender::SetSurface(VkSurfaceKHR surface) { m_surface = surface; }
void VulkanRender::SetInstance(VkInstance instance) { m_instance = instance; m_externalInstance = true; }
void VulkanRender::SetFramebufferSize(uint32_t width, uint32_t height) { m_framebufferWidth = width; m_framebufferHeight = height; }
void VulkanRender::SetFramebufferResized(bool resized) { m_framebufferResized = resized; }

void VulkanRender::OnMouseDown(float nx, float ny, int button) { (void)nx; (void)ny; (void)button; }
void VulkanRender::OnMouseMove(float nx, float ny) { (void)nx; (void)ny; }
void VulkanRender::OnMouseUp(int button) { (void)button; }
void VulkanRender::OnMouseWheel(float delta) { (void)delta; }
