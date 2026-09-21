#include "VKRender.h"
#include "Path/AssetPath.h"
#include "Texture/VKTexture.h"
#include "Object/Object.h"
#include <iostream>
#include <fstream>
#include <set>
#include <algorithm>
#include <stdexcept>
#include <cstring>
#include <QRunnable>
#include <QThreadPool>
#include <QCoreApplication>
#include <utility>

namespace {

class VKFunctionRunnable final : public QRunnable {
public:
    explicit VKFunctionRunnable(Render::AsyncTask task)
        : m_task(std::move(task)) {}

    // 执行后台任务。
    void run() override {
        if (m_task) m_task();
    }

private:
    Render::AsyncTask m_task;
};

}

#define VK_CHECK_RESULT(result, msg) \
    if ((result) != VK_SUCCESS) { \
        return Fail(std::string(msg) + " (错误码: " + std::to_string(result) + ")"); \
    }

// 判断图形队列和呈现队列是否均已找到。
bool VulkanQueueFamilyIndices::IsComplete() const { return graphicsFamily >= 0 && presentFamily >= 0; }


VKRender::VKRender()
    : m_asyncThreadPool(std::make_unique<QThreadPool>()) {
    m_clearValues[0].color = { 0.1f, 0.1f, 0.12f, 1.0f };
    m_clearValueCount = 1;
}

VKRender::~VKRender() {
    Shutdown();
}


// 初始化渲染器及其后端资源。
bool VKRender::Initialize(const char* appName, uint32_t width, uint32_t height) {
    m_framebufferWidth = width;
    m_framebufferHeight = height;
    m_deviceLost = false;
    m_lastError.clear();

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

    m_shuttingDown = false;
    m_frameRecording = false;

    if (!OnInitialize()) {
        if (m_lastError.empty()) SetLastError("后端初始化失败");
        return false;
    }

    if (!CreateRenderPass()) return false;

    try {
        if (!CreatePipelines()) {
            if (m_lastError.empty()) SetLastError("创建图形管线失败");
            return false;
        }
    } catch (const std::exception& e) {
        SetLastError(e.what());
        std::cerr << e.what() << std::endl;
        return false;
    }

    if (!CreateFramebuffers()) return false;

    if (!CreateCommandPool()) return false;
    if (!CreateCommandBuffers()) return false;
    if (!CreateSyncObjects()) return false;

    m_initialized = true;
    return true;
}

// 等待异步任务完成并使渲染器静止。
void VKRender::Quiesce() {
    m_shuttingDown = true;

    if (m_asyncThreadPool) {
        m_asyncThreadPool->waitForDone();
    }
    QCoreApplication::processEvents(QEventLoop::ExcludeUserInputEvents);

    if (m_device != VK_NULL_HANDLE) {
        vkDeviceWaitIdle(m_device);
    }
}

// 关闭渲染器并释放资源。
void VKRender::Shutdown() {
    if (!m_initialized && m_device == VK_NULL_HANDLE && m_swapchain == VK_NULL_HANDLE) {
        return;
    }

    if (m_initialized || m_device != VK_NULL_HANDLE) {
        Quiesce();
    }

    // 纹理使用单次命令池，须在销毁命令池前释放。
    ReleaseAllTextures();

    CleanupSwapchain();

    if (m_device != VK_NULL_HANDLE) {
        for (VkSemaphore semaphore : m_imageAvailableSemaphores) {
            if (semaphore != VK_NULL_HANDLE) {
                vkDestroySemaphore(m_device, semaphore, nullptr);
            }
        }
        for (VkSemaphore semaphore : m_renderFinishedSemaphores) {
            if (semaphore != VK_NULL_HANDLE) {
                vkDestroySemaphore(m_device, semaphore, nullptr);
            }
        }
        for (VkFence fence : m_inFlightFences) {
            if (fence != VK_NULL_HANDLE) {
                vkDestroyFence(m_device, fence, nullptr);
            }
        }
    }

    if (m_singleTimeCommandPool != VK_NULL_HANDLE) {
        vkDestroyCommandPool(m_device, m_singleTimeCommandPool, nullptr);
        m_singleTimeCommandPool = VK_NULL_HANDLE;
    }
    if (m_commandPool != VK_NULL_HANDLE) {
        vkDestroyCommandPool(m_device, m_commandPool, nullptr);
        m_commandPool = VK_NULL_HANDLE;
    }

    OnDestroyPipelines();
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

    OnShutdown();

    if (m_device != VK_NULL_HANDLE) {
        vkDestroyDevice(m_device, nullptr);
        m_device = VK_NULL_HANDLE;
    }
    m_physicalDevice = VK_NULL_HANDLE;
    m_graphicsQueue = VK_NULL_HANDLE;
    m_presentQueue = VK_NULL_HANDLE;
    m_commandPool = VK_NULL_HANDLE;
    m_commandBuffers.clear();
    m_imageAvailableSemaphores.clear();
    m_renderFinishedSemaphores.clear();
    m_inFlightFences.clear();
    m_currentFrame = 0;
    m_imageIndex = 0;
    m_frameRecording = false;

    if (m_debugMessenger != VK_NULL_HANDLE) {
        auto func = (PFN_vkDestroyDebugUtilsMessengerEXT)vkGetInstanceProcAddr(m_instance, "vkDestroyDebugUtilsMessengerEXT");
        if (func) func(m_instance, m_debugMessenger, nullptr);
        m_debugMessenger = VK_NULL_HANDLE;
    }

    if (!m_externalInstance && m_instance != VK_NULL_HANDLE) {
        vkDestroyInstance(m_instance, nullptr);
        m_instance = VK_NULL_HANDLE;
    }

    m_initialized = false;
    m_shuttingDown = false;
    m_deviceLost = false;
}

// 当前帧缓冲宽高是否可用于绘制。
bool VKRender::HasUsableFramebuffer() const {
    return m_framebufferWidth > 0 && m_framebufferHeight > 0;
}

// 记录失败并返回 false。
bool VKRender::Fail(const std::string& message) {
    SetLastError(message);
    std::cerr << message << std::endl;
    return false;
}





// 确保本帧已开始录制命令。
bool VKRender::EnsureFrameRecording() {
    if (m_frameRecording) return true;
    if (m_deviceLost || !m_initialized || m_device == VK_NULL_HANDLE) return false;
    if (m_swapchain == VK_NULL_HANDLE || !HasUsableFramebuffer()) return false;

    if (m_framebufferResized) {
        m_framebufferResized = false;
        if (!RecreateSwapchain()) return false;
    }

    vkWaitForFences(m_device, 1, &m_inFlightFences[m_currentFrame], VK_TRUE, UINT64_MAX);

    uint32_t imageIndex;
    VkResult result = vkAcquireNextImageKHR(
        m_device, m_swapchain, UINT64_MAX,
        m_imageAvailableSemaphores[m_currentFrame], VK_NULL_HANDLE, &imageIndex);

    if (result == VK_ERROR_OUT_OF_DATE_KHR) {
        RecreateSwapchain();
        return false;
    }
    if (result == VK_ERROR_DEVICE_LOST || result == VK_ERROR_SURFACE_LOST_KHR) {
        MarkDeviceLost(result == VK_ERROR_DEVICE_LOST
            ? "Vulkan 设备已丢失，请重启应用"
            : "Vulkan 表面已丢失，请重启应用");
        return false;
    }
    if (result != VK_SUCCESS && result != VK_SUBOPTIMAL_KHR) {
        Fail("获取交换链图像失败 (错误码: " + std::to_string(result) + ")");
        return false;
    }

    m_imageIndex = imageIndex;

    vkResetFences(m_device, 1, &m_inFlightFences[m_currentFrame]);

    vkResetCommandBuffer(m_commandBuffers[m_currentFrame], 0);
    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    vkBeginCommandBuffer(m_commandBuffers[m_currentFrame], &beginInfo);
    m_colorPassOpen = false;
    m_frameRecording = true;
    return true;
}

// 开始主颜色渲染通道。
void VKRender::BeginColorRenderPass() {
    OnBeginFrame();

    VkRenderPassBeginInfo renderPassInfo{};
    renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    renderPassInfo.renderPass = m_renderPass;
    renderPassInfo.framebuffer = m_swapchainFramebuffers[m_imageIndex];
    renderPassInfo.renderArea.offset = { 0, 0 };
    renderPassInfo.renderArea.extent = m_swapchainExtent;

    renderPassInfo.clearValueCount = m_clearValueCount;
    renderPassInfo.pClearValues = m_clearValues;

    vkCmdBeginRenderPass(m_commandBuffers[m_currentFrame], &renderPassInfo, VK_SUBPASS_CONTENTS_INLINE);

    VkViewport viewport{};
    viewport.x = 0.0f;
    viewport.y = 0.0f;
    viewport.width = static_cast<float>(m_swapchainExtent.width);
    viewport.height = static_cast<float>(m_swapchainExtent.height);
    viewport.minDepth = 0.0f;
    viewport.maxDepth = 1.0f;
    vkCmdSetViewport(m_commandBuffers[m_currentFrame], 0, 1, &viewport);

    VkRect2D scissor{};
    scissor.offset = { 0, 0 };
    scissor.extent = m_swapchainExtent;
    vkCmdSetScissor(m_commandBuffers[m_currentFrame], 0, 1, &scissor);

    m_colorPassOpen = true;
}

// 结束当前颜色渲染通道（1.6：HDR 后处理需要先结束场景通道再开新的通道）。
void VKRender::EndColorRenderPass() {
    if (!m_colorPassOpen) return;
    vkCmdEndRenderPass(m_commandBuffers[m_currentFrame]);
    m_colorPassOpen = false;
}

// 开始一帧渲染。
bool VKRender::BeginFrame() {
    if (m_deviceLost || !m_initialized) return false;
    if (!HasUsableFramebuffer()) return false;
    OnPrepareFrame();
    if (!EnsureFrameRecording()) return false;
    BeginColorRenderPass();
    return true;
}

// 结束当前帧并提交结果。
void VKRender::EndFrame() {
    if (!m_frameRecording) return;

    // 主通道结束前刷新透明绘制（按深度排序）。
    FlushTransparentDraws();

    // 1.6：场景通道结束后、关闭当前通道前执行 HDR 后处理（子类实现）。
    OnAfterScenePass();

    // 场景通道由 BeginColorRenderPass 打开；后处理可能在 OnAfterScenePass 中提前结束它。
    if (m_colorPassOpen) {
        vkCmdEndRenderPass(m_commandBuffers[m_currentFrame]);
        m_colorPassOpen = false;
    }

    OnEndFrame();

    vkEndCommandBuffer(m_commandBuffers[m_currentFrame]);

    // 推进纹理延迟销毁队列（每帧一次）。
    ProcessDeferredTextureDestruction();

    VkSubmitInfo submitInfo{};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;

    VkSemaphore waitSemaphores[] = { m_imageAvailableSemaphores[m_currentFrame] };
    VkPipelineStageFlags waitStages[] = { VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT };
    submitInfo.waitSemaphoreCount = 1;
    submitInfo.pWaitSemaphores = waitSemaphores;
    submitInfo.pWaitDstStageMask = waitStages;
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &m_commandBuffers[m_currentFrame];

    VkSemaphore signalSemaphores[] = { m_renderFinishedSemaphores[m_currentFrame] };
    submitInfo.signalSemaphoreCount = 1;
    submitInfo.pSignalSemaphores = signalSemaphores;

    const VkResult submitResult = vkQueueSubmit(
        m_graphicsQueue, 1, &submitInfo, m_inFlightFences[m_currentFrame]);
    if (submitResult == VK_ERROR_DEVICE_LOST) {
        m_frameRecording = false;
        MarkDeviceLost("Vulkan 设备已丢失，请重启应用");
        return;
    }
    if (submitResult != VK_SUCCESS) {
        m_frameRecording = false;
        Fail("提交命令缓冲区失败 (错误码: " + std::to_string(submitResult) + ")");
        MarkDeviceLost(m_lastError);
        return;
    }

    VkPresentInfoKHR presentInfo{};
    presentInfo.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
    presentInfo.waitSemaphoreCount = 1;
    presentInfo.pWaitSemaphores = signalSemaphores;

    VkSwapchainKHR swapChains[] = { m_swapchain };
    presentInfo.swapchainCount = 1;
    presentInfo.pSwapchains = swapChains;
    presentInfo.pImageIndices = &m_imageIndex;

    VkResult result = vkQueuePresentKHR(m_presentQueue, &presentInfo);

    m_currentFrame = (m_currentFrame + 1) % MAX_FRAMES_IN_FLIGHT;
    m_frameRecording = false;

    if (result == VK_ERROR_DEVICE_LOST || result == VK_ERROR_SURFACE_LOST_KHR) {
        MarkDeviceLost(result == VK_ERROR_DEVICE_LOST
            ? "Vulkan 设备已丢失，请重启应用"
            : "Vulkan 表面已丢失，请重启应用");
        return;
    }
    if (result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_SUBOPTIMAL_KHR || m_framebufferResized) {
        m_framebufferResized = false;
        RecreateSwapchain();
    } else if (result != VK_SUCCESS) {
        Fail("呈现图像失败 (错误码: " + std::to_string(result) + ")");
        MarkDeviceLost(m_lastError);
    }
}

// 提交索引绘制命令。
void VKRender::DrawIndexed(uint32_t indexCount, uint32_t instanceCount) {
    vkCmdDrawIndexed(m_commandBuffers[m_currentFrame], indexCount, instanceCount, 0, 0, 0);
}

// 提交带起始索引的索引绘制（SubMesh 范围）。
void VKRender::DrawIndexedRange(uint32_t indexCount, uint32_t firstIndex,
                                uint32_t vertexOffset) {
    if (indexCount == 0) return;
    vkCmdDrawIndexed(m_commandBuffers[m_currentFrame], indexCount, 1,
                     firstIndex, static_cast<int32_t>(vertexOffset), 0);
}

// 创建一张 GPU 纹理（staging 上传 + 布局转换 + mipmap）。
// 若 desc.cacheKey 非空且已存在，则复用同一纹理并增加引用计数。
TextureHandle VKRender::CreateTexture(const TextureDesc& desc) {
    if (m_device == VK_NULL_HANDLE || m_physicalDevice == VK_NULL_HANDLE) return 0;
    if (m_singleTimeCommandPool == VK_NULL_HANDLE || m_graphicsQueue == VK_NULL_HANDLE) return 0;

    // 命中已有键：复用并增加引用计数。
    if (!desc.cacheKey.empty()) {
        const auto found = m_textureKeyToHandle.find(desc.cacheKey);
        if (found != m_textureKeyToHandle.end()) {
            ++m_textureRefCount[found->second];
            return found->second;
        }
    }

    auto texture = std::make_unique<VKTexture>();
    VKTextureContext context{};
    context.device = m_device;
    context.physicalDevice = m_physicalDevice;
    context.commandPool = m_singleTimeCommandPool;
    context.queue = m_graphicsQueue;
    if (!texture->Create(context, desc)) {
        return 0;
    }

    const TextureHandle handle = m_nextTextureHandle++;
    m_textures.emplace(handle, std::move(texture));
    m_textureRefCount[handle] = 1;
    if (!desc.cacheKey.empty()) {
        m_textureKeyToHandle.emplace(desc.cacheKey, handle);
    }
    return handle;
}

// 标记释放纹理：递减引用计数，归零才进入延迟销毁队列。
void VKRender::DestroyTexture(TextureHandle handle) {
    if (handle == 0) return;
    if (m_textures.find(handle) == m_textures.end()) return;

    auto ref = m_textureRefCount.find(handle);
    if (ref != m_textureRefCount.end() && --ref->second > 0) {
        return;  // 仍被其他模型/材质引用
    }
    for (const auto& entry : m_deferredTextureDestruction) {
        if (entry.first == handle) return;  // 已在队列中
    }
    m_deferredTextureDestruction.emplace_back(handle, m_textureFrameCounter);
}

// 推进延迟销毁队列：入队超过 kTextureDestroyDelayFrames 帧的纹理才真正销毁。
void VKRender::ProcessDeferredTextureDestruction() {
    ++m_textureFrameCounter;
    if (m_deferredTextureDestruction.empty()) return;

    const uint64_t safeFrame =
        m_textureFrameCounter > kTextureDestroyDelayFrames
            ? m_textureFrameCounter - kTextureDestroyDelayFrames : 0;

    for (auto it = m_deferredTextureDestruction.begin();
         it != m_deferredTextureDestruction.end();) {
        if (it->second <= safeFrame) {
            auto found = m_textures.find(it->first);
            if (found != m_textures.end()) {
                // 从键表移除指向该句柄的映射。
                for (auto keyIt = m_textureKeyToHandle.begin();
                     keyIt != m_textureKeyToHandle.end();) {
                    if (keyIt->second == it->first) {
                        keyIt = m_textureKeyToHandle.erase(keyIt);
                    } else {
                        ++keyIt;
                    }
                }
                m_textureRefCount.erase(it->first);
                OnTextureDestroyed(it->first);
                found->second->Destroy();
                m_textures.erase(found);
            }
            it = m_deferredTextureDestruction.erase(it);
        } else {
            ++it;
        }
    }
}

// 立即销毁全部纹理（先等待 GPU 空闲）。
void VKRender::ReleaseAllTextures() {
    if (m_textures.empty() && m_deferredTextureDestruction.empty()) return;
    if (m_device != VK_NULL_HANDLE) {
        vkDeviceWaitIdle(m_device);
    }
    for (auto& entry : m_textures) {
        OnTextureDestroyed(entry.first);
        entry.second->Destroy();
    }
    m_textures.clear();
    m_textureKeyToHandle.clear();
    m_textureRefCount.clear();
    m_deferredTextureDestruction.clear();
}

// 查询句柄是否有效。
bool VKRender::IsTextureValid(TextureHandle handle) const {
    return handle != 0 && m_textures.find(handle) != m_textures.end();
}

// 返回纹理的 VkImageView。
VkImageView VKRender::GetTextureImageView(TextureHandle handle) const {
    const auto found = m_textures.find(handle);
    return found == m_textures.end() ? VK_NULL_HANDLE : found->second->GetImageView();
}

// 返回纹理的 VkSampler。
VkSampler VKRender::GetTextureSampler(TextureHandle handle) const {
    const auto found = m_textures.find(handle);
    return found == m_textures.end() ? VK_NULL_HANDLE : found->second->GetSampler();
}


// 创建 Vulkan 缓冲区并绑定内存。
VkBuffer VKRender::CreateBuffer(VkDeviceSize size, VkBufferUsageFlags usage, VkMemoryPropertyFlags properties) {
    VkBuffer buffer;
    VkBufferCreateInfo bufferInfo{};
    bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufferInfo.size = size;
    bufferInfo.usage = usage;
    bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    if (vkCreateBuffer(m_device, &bufferInfo, nullptr, &buffer) != VK_SUCCESS) {
        throw std::runtime_error("创建缓冲区失败");
    }

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

    vkBindBufferMemory(m_device, buffer, bufferMemory, 0);

    m_bufferMemoryMap[buffer] = bufferMemory;
    return buffer;
}

// 销毁缓冲区及其设备内存。
void VKRender::DestroyBuffer(VkBuffer buffer) {
    if (buffer == VK_NULL_HANDLE) return;

    auto it = m_bufferMemoryMap.find(buffer);
    if (it != m_bufferMemoryMap.end()) {
        vkFreeMemory(m_device, it->second, nullptr);
        m_bufferMemoryMap.erase(it);
    }
    vkDestroyBuffer(m_device, buffer, nullptr);
}

// 映射 Vulkan 缓冲区内存。
VkResult VKRender::MapBuffer(VkBuffer buffer, void** data) {
    auto it = m_bufferMemoryMap.find(buffer);
    if (it == m_bufferMemoryMap.end()) {
        return VK_ERROR_MEMORY_MAP_FAILED;
    }
    return vkMapMemory(m_device, it->second, 0, VK_WHOLE_SIZE, 0, data);
}

// 解除 Vulkan 缓冲区内存映射。
void VKRender::UnmapBuffer(VkBuffer buffer) {
    auto it = m_bufferMemoryMap.find(buffer);
    if (it != m_bufferMemoryMap.end()) {
        vkUnmapMemory(m_device, it->second);
    }
}

// 分配 Vulkan 设备内存。
VkDeviceMemory VKRender::AllocateMemory(VkMemoryRequirements memRequirements, VkMemoryPropertyFlags properties) {
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


// 开始一次性命令缓冲。
VkCommandBuffer VKRender::BeginSingleTimeCommands() {
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

// 提交并等待一次性命令缓冲。
void VKRender::EndSingleTimeCommands(VkCommandBuffer commandBuffer) {
    vkEndCommandBuffer(commandBuffer);

    VkSubmitInfo submitInfo{};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &commandBuffer;

    vkQueueSubmit(m_graphicsQueue, 1, &submitInfo, VK_NULL_HANDLE);
    vkQueueWaitIdle(m_graphicsQueue);

    vkFreeCommandBuffers(m_device, m_singleTimeCommandPool, 1, &commandBuffer);
}

// 复制 Vulkan 缓冲区内容。
void VKRender::CopyBuffer(VkBuffer srcBuffer, VkBuffer dstBuffer, VkDeviceSize size,
                          VkDeviceSize srcOffset, VkDeviceSize dstOffset) {
    VkCommandBuffer commandBuffer = BeginSingleTimeCommands();

    VkBufferCopy copyRegion{};
    copyRegion.srcOffset = srcOffset;
    copyRegion.dstOffset = dstOffset;
    copyRegion.size = size;
    vkCmdCopyBuffer(commandBuffer, srcBuffer, dstBuffer, 1, &copyRegion);

    EndSingleTimeCommands(commandBuffer);
}

// 从 staging 缓冲区创建设备缓冲区。
VkBuffer VKRender::CreateBufferFromStaging(VkBuffer stagingBuffer, VkDeviceSize size,
                                           VkBufferUsageFlags usage,
                                           VkDeviceSize stagingOffset) {
    VkBuffer deviceBuffer = CreateBuffer(
        size, VK_BUFFER_USAGE_TRANSFER_DST_BIT | usage,
        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    try {
        CopyBuffer(stagingBuffer, deviceBuffer, size, stagingOffset);
    } catch (...) {
        DestroyBuffer(deviceBuffer);
        throw;
    }
    return deviceBuffer;
}

// 等待 GPU 与异步任务完成。
void VKRender::WaitForIdle() {
    vkDeviceWaitIdle(m_device);
}


// 检查 Vulkan 验证层支持。
bool VKRender::CheckValidationLayerSupport() {
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

// 返回实例所需扩展名。
std::vector<const char*> VKRender::GetRequiredExtensions() {
    std::vector<const char*> extensions;
    extensions.push_back(VK_KHR_SURFACE_EXTENSION_NAME);
    extensions.push_back("VK_KHR_win32_surface");

#ifndef NDEBUG
    extensions.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
#endif

    return extensions;
}


// 创建 VkInstance。
bool VKRender::CreateInstance(const char* appName) {
    if (m_enableValidationLayers && !CheckValidationLayerSupport()) {
        return Fail("Vulkan 验证层不可用");
    }

    VkApplicationInfo appInfo{};
    appInfo.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    appInfo.pApplicationName = appName;
    appInfo.applicationVersion = VK_MAKE_VERSION(1, 0, 0);
    appInfo.pEngineName = "VulkanEngine";
    appInfo.engineVersion = VK_MAKE_VERSION(1, 0, 0);
    appInfo.apiVersion = VK_API_VERSION_1_0;

    VkInstanceCreateInfo createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    createInfo.pApplicationInfo = &appInfo;

    auto extensions = GetRequiredExtensions();
    createInfo.enabledExtensionCount = static_cast<uint32_t>(extensions.size());
    createInfo.ppEnabledExtensionNames = extensions.data();

    VkDebugUtilsMessengerCreateInfoEXT debugCreateInfo{};
    if (m_enableValidationLayers) {
        createInfo.enabledLayerCount = static_cast<uint32_t>(m_validationLayers.size());
        createInfo.ppEnabledLayerNames = m_validationLayers.data();

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

    VkResult result = vkCreateInstance(&createInfo, nullptr, &m_instance);
    if (result != VK_SUCCESS) {
        return Fail("创建 Vulkan Instance 失败");
    }

    return true;
}

// 创建验证层调试回调。
bool VKRender::SetupDebugMessenger() {
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
            return Fail("设置 Vulkan 调试回调失败");
        }
    }

    return true;
}

// 选择合适的 Vulkan 物理设备。
bool VKRender::PickPhysicalDevice() {
    uint32_t deviceCount = 0;
    vkEnumeratePhysicalDevices(m_instance, &deviceCount, nullptr);

    if (deviceCount == 0) {
        return Fail("未找到支持 Vulkan 的 GPU");
    }

    std::vector<VkPhysicalDevice> devices(deviceCount);
    vkEnumeratePhysicalDevices(m_instance, &deviceCount, devices.data());

    int bestScore = -1;
    for (const auto& device : devices) {
        int score = RateDevice(device);
        if (score > bestScore && IsDeviceSuitable(device)) {
            bestScore = score;
            m_physicalDevice = device;
        }
    }

    if (m_physicalDevice == VK_NULL_HANDLE) {
        return Fail("未找到合适的 Vulkan GPU");
    }

    return true;
}

// 创建逻辑设备与队列。
bool VKRender::CreateLogicalDevice() {
    VulkanQueueFamilyIndices indices = FindQueueFamilies(m_physicalDevice);

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

    VkPhysicalDeviceFeatures deviceFeatures{};
    deviceFeatures.samplerAnisotropy = VK_TRUE;  // 启用各向异性过滤
    deviceFeatures.fillModeNonSolid = VK_TRUE;   // 启用线框模式

    VkDeviceCreateInfo createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
    createInfo.queueCreateInfoCount = static_cast<uint32_t>(queueCreateInfos.size());
    createInfo.pQueueCreateInfos = queueCreateInfos.data();
    createInfo.pEnabledFeatures = &deviceFeatures;
    createInfo.enabledExtensionCount = static_cast<uint32_t>(m_deviceExtensions.size());
    createInfo.ppEnabledExtensionNames = m_deviceExtensions.data();

    VkResult result = vkCreateDevice(m_physicalDevice, &createInfo, nullptr, &m_device);
    if (result != VK_SUCCESS) {
        return Fail("创建 Vulkan 逻辑设备失败");
    }

    vkGetDeviceQueue(m_device, indices.graphicsFamily, 0, &m_graphicsQueue);
    vkGetDeviceQueue(m_device, indices.presentFamily, 0, &m_presentQueue);

    return true;
}

// 创建交换链。
bool VKRender::CreateSwapchain() {
    if (!HasUsableFramebuffer()) {
        return Fail("窗口尺寸为 0，无法创建 Swapchain");
    }

    VulkanSwapchainSupportDetails swapChainSupport = QuerySwapchainSupport(m_physicalDevice);

    if (swapChainSupport.formats.empty() || swapChainSupport.presentModes.empty()) {
        return Fail("Surface 不支持 Swapchain 格式或呈现模式");
    }

    VkSurfaceFormatKHR surfaceFormat = ChooseSwapSurfaceFormat(swapChainSupport.formats);
    VkPresentModeKHR presentMode = ChooseSwapPresentMode(swapChainSupport.presentModes);
    VkExtent2D extent = ChooseSwapExtent(swapChainSupport.capabilities);
    if (extent.width == 0 || extent.height == 0) {
        return false;
    }

    uint32_t imageCount = swapChainSupport.capabilities.minImageCount + 1;
    if (swapChainSupport.capabilities.maxImageCount > 0 && imageCount > swapChainSupport.capabilities.maxImageCount) {
        imageCount = swapChainSupport.capabilities.maxImageCount;
    }

    VkSwapchainCreateInfoKHR createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
    createInfo.surface = m_surface;
    createInfo.minImageCount = imageCount;
    createInfo.imageFormat = surfaceFormat.format;
    createInfo.imageColorSpace = surfaceFormat.colorSpace;
    createInfo.imageExtent = extent;
    createInfo.imageArrayLayers = 1;
    createInfo.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;

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
        return Fail("创建 Swapchain 失败");
    }

    vkGetSwapchainImagesKHR(m_device, m_swapchain, &imageCount, nullptr);
    m_swapchainImages.resize(imageCount);
    vkGetSwapchainImagesKHR(m_device, m_swapchain, &imageCount, m_swapchainImages.data());

    m_swapchainImageFormat = surfaceFormat.format;
    m_swapchainExtent = extent;

    return true;
}

// 重建 Vulkan 交换链。
bool VKRender::RecreateSwapchain() {
    if (m_deviceLost || m_device == VK_NULL_HANDLE) return false;
    if (!HasUsableFramebuffer()) return false;

    vkDeviceWaitIdle(m_device);

    CleanupSwapchain();
    OnRecreateSwapchain();

    if (!CreateSwapchain()) return false;
    if (!CreateImageViews()) return false;
    if (!CreateFramebuffers()) return false;

    m_framebufferResized = false;
    m_frameRecording = false;
    return true;
}

// 为交换链图像创建视图。
bool VKRender::CreateImageViews() {
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
            return Fail("创建 Swapchain 图像视图失败");
        }
    }

    return true;
}

// 创建渲染通道。
bool VKRender::CreateRenderPass() {
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

    VkSubpassDependency dependency{};
    dependency.srcSubpass = VK_SUBPASS_EXTERNAL;
    dependency.dstSubpass = 0;
    dependency.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    dependency.srcAccessMask = 0;
    dependency.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    dependency.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;

    VkRenderPassCreateInfo renderPassInfo{};
    renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    renderPassInfo.attachmentCount = 1;
    renderPassInfo.pAttachments = &colorAttachment;
    renderPassInfo.subpassCount = 1;
    renderPassInfo.pSubpasses = &subpass;
    renderPassInfo.dependencyCount = 1;
    renderPassInfo.pDependencies = &dependency;

    if (vkCreateRenderPass(m_device, &renderPassInfo, nullptr, &m_renderPass) != VK_SUCCESS) {
        return Fail("创建 RenderPass 失败");
    }

    return true;
}

// 创建图形管线。
bool VKRender::CreatePipelines() {
    return true;
}

// 创建帧缓冲。
bool VKRender::CreateFramebuffers() {
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
            return Fail("创建 Framebuffer 失败");
        }
    }

    return true;
}

// 创建命令池。
bool VKRender::CreateCommandPool() {
    VulkanQueueFamilyIndices queueFamilyIndices = FindQueueFamilies(m_physicalDevice);

    VkCommandPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    poolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    poolInfo.queueFamilyIndex = queueFamilyIndices.graphicsFamily;

    if (vkCreateCommandPool(m_device, &poolInfo, nullptr, &m_commandPool) != VK_SUCCESS) {
        return Fail("创建命令池失败");
    }

    poolInfo.flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT;
    if (vkCreateCommandPool(m_device, &poolInfo, nullptr, &m_singleTimeCommandPool) != VK_SUCCESS) {
        return Fail("创建单次命令池失败");
    }

    return true;
}

// 分配每帧命令缓冲。
bool VKRender::CreateCommandBuffers() {
    m_commandBuffers.resize(MAX_FRAMES_IN_FLIGHT);

    VkCommandBufferAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    allocInfo.commandPool = m_commandPool;
    allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocInfo.commandBufferCount = static_cast<uint32_t>(m_commandBuffers.size());

    if (vkAllocateCommandBuffers(m_device, &allocInfo, m_commandBuffers.data()) != VK_SUCCESS) {
        return Fail("分配命令缓冲区失败");
    }

    return true;
}

// 创建信号量与围栏。
bool VKRender::CreateSyncObjects() {
    m_imageAvailableSemaphores.assign(MAX_FRAMES_IN_FLIGHT, VK_NULL_HANDLE);
    m_renderFinishedSemaphores.assign(MAX_FRAMES_IN_FLIGHT, VK_NULL_HANDLE);
    m_inFlightFences.assign(MAX_FRAMES_IN_FLIGHT, VK_NULL_HANDLE);

    VkSemaphoreCreateInfo semaphoreInfo{};
    semaphoreInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;

    VkFenceCreateInfo fenceInfo{};
    fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    fenceInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;  // 初始状态为已信号

    for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
        if (vkCreateSemaphore(m_device, &semaphoreInfo, nullptr, &m_imageAvailableSemaphores[i]) != VK_SUCCESS ||
            vkCreateSemaphore(m_device, &semaphoreInfo, nullptr, &m_renderFinishedSemaphores[i]) != VK_SUCCESS ||
            vkCreateFence(m_device, &fenceInfo, nullptr, &m_inFlightFences[i]) != VK_SUCCESS) {
            return Fail("创建 Fence/Semaphore 失败");
        }
    }

    return true;
}

// 清理 Vulkan 交换链资源。
void VKRender::CleanupSwapchain() {
    // 1.6：先释放尺寸相关的 HDR 中间目标（引用交换链尺寸）。
    OnBeforeCleanupSwapchain();
    if (m_device != VK_NULL_HANDLE) {
        for (auto framebuffer : m_swapchainFramebuffers) {
            if (framebuffer != VK_NULL_HANDLE) {
                vkDestroyFramebuffer(m_device, framebuffer, nullptr);
            }
        }
        for (auto imageView : m_swapchainImageViews) {
            if (imageView != VK_NULL_HANDLE) {
                vkDestroyImageView(m_device, imageView, nullptr);
            }
        }
        if (m_swapchain != VK_NULL_HANDLE) {
            vkDestroySwapchainKHR(m_device, m_swapchain, nullptr);
        }
    }
    m_swapchainFramebuffers.clear();
    m_swapchainImageViews.clear();
    m_swapchainImages.clear();
    m_swapchain = VK_NULL_HANDLE;
}


// 查找图形与呈现队列族。
VulkanQueueFamilyIndices VKRender::FindQueueFamilies(VkPhysicalDevice device) {
    VulkanQueueFamilyIndices indices;

    uint32_t queueFamilyCount = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(device, &queueFamilyCount, nullptr);

    std::vector<VkQueueFamilyProperties> queueFamilies(queueFamilyCount);
    vkGetPhysicalDeviceQueueFamilyProperties(device, &queueFamilyCount, queueFamilies.data());

    int i = 0;
    for (const auto& queueFamily : queueFamilies) {
        if (queueFamily.queueFlags & VK_QUEUE_GRAPHICS_BIT) {
            indices.graphicsFamily = i;
        }

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

// 查询表面格式与呈现模式。
VulkanSwapchainSupportDetails VKRender::QuerySwapchainSupport(VkPhysicalDevice device) {
    VulkanSwapchainSupportDetails details;

    vkGetPhysicalDeviceSurfaceCapabilitiesKHR(device, m_surface, &details.capabilities);

    uint32_t formatCount;
    vkGetPhysicalDeviceSurfaceFormatsKHR(device, m_surface, &formatCount, nullptr);
    if (formatCount != 0) {
        details.formats.resize(formatCount);
        vkGetPhysicalDeviceSurfaceFormatsKHR(device, m_surface, &formatCount, details.formats.data());
    }

    uint32_t presentModeCount;
    vkGetPhysicalDeviceSurfacePresentModesKHR(device, m_surface, &presentModeCount, nullptr);
    if (presentModeCount != 0) {
        details.presentModes.resize(presentModeCount);
        vkGetPhysicalDeviceSurfacePresentModesKHR(device, m_surface, &presentModeCount, details.presentModes.data());
    }

    return details;
}

// 判断物理设备是否满足交换链需求。
bool VKRender::IsDeviceSuitable(VkPhysicalDevice device) {
    VulkanQueueFamilyIndices indices = FindQueueFamilies(device);

    uint32_t extensionCount;
    vkEnumerateDeviceExtensionProperties(device, nullptr, &extensionCount, nullptr);

    std::vector<VkExtensionProperties> availableExtensions(extensionCount);
    vkEnumerateDeviceExtensionProperties(device, nullptr, &extensionCount, availableExtensions.data());

    std::set<std::string> requiredExtensions(m_deviceExtensions.begin(), m_deviceExtensions.end());
    for (const auto& extension : availableExtensions) {
        requiredExtensions.erase(extension.extensionName);
    }

    bool extensionsSupported = requiredExtensions.empty();
    bool swapChainAdequate = false;
    if (extensionsSupported) {
        VulkanSwapchainSupportDetails swapChainSupport = QuerySwapchainSupport(device);
        swapChainAdequate = !swapChainSupport.formats.empty() && !swapChainSupport.presentModes.empty();
    }

    return indices.IsComplete() && extensionsSupported && swapChainAdequate;
}

// 评估 Vulkan 物理设备。
int VKRender::RateDevice(VkPhysicalDevice device) {
    VkPhysicalDeviceProperties deviceProperties;
    vkGetPhysicalDeviceProperties(device, &deviceProperties);

    VkPhysicalDeviceFeatures deviceFeatures;
    vkGetPhysicalDeviceFeatures(device, &deviceFeatures);

    int score = 0;

    if (deviceProperties.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU) {
        score += 10000;
    }

    score += deviceProperties.limits.maxImageDimension2D;

    if (!deviceFeatures.geometryShader) {
        return 0;
    }

    return score;
}


// 选择交换链表面格式。
VkSurfaceFormatKHR VKRender::ChooseSwapSurfaceFormat(const std::vector<VkSurfaceFormatKHR>& availableFormats) {
    for (const auto& availableFormat : availableFormats) {
        if (availableFormat.format == VK_FORMAT_B8G8R8A8_SRGB &&
            availableFormat.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR) {
            return availableFormat;
        }
    }
    return availableFormats[0];
}

// 选择呈现模式。
VkPresentModeKHR VKRender::ChooseSwapPresentMode(const std::vector<VkPresentModeKHR>& availablePresentModes) {
    for (const auto& availablePresentMode : availablePresentModes) {
        if (availablePresentMode == VK_PRESENT_MODE_MAILBOX_KHR) {
            return availablePresentMode;
        }
    }
    return VK_PRESENT_MODE_FIFO_KHR;
}

// 选择交换链分辨率。
VkExtent2D VKRender::ChooseSwapExtent(const VkSurfaceCapabilitiesKHR& capabilities) {
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


// 按过滤条件查找内存类型索引。
uint32_t VKRender::FindMemoryType(uint32_t typeFilter, VkMemoryPropertyFlags properties) {
    VkPhysicalDeviceMemoryProperties memProperties;
    vkGetPhysicalDeviceMemoryProperties(m_physicalDevice, &memProperties);

    for (uint32_t i = 0; i < memProperties.memoryTypeCount; i++) {
        if ((typeFilter & (1 << i)) && (memProperties.memoryTypes[i].propertyFlags & properties) == properties) {
            return i;
        }
    }

    throw std::runtime_error("未找到合适的内存类型");
}

// 从 SPIR-V 创建着色器模块。
VkResult VKRender::CreateShaderModule(const std::vector<char>& code, VkShaderModule* shaderModule) {
    VkShaderModuleCreateInfo createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    createInfo.codeSize = code.size();
    createInfo.pCode = reinterpret_cast<const uint32_t*>(code.data());

    return vkCreateShaderModule(m_device, &createInfo, nullptr, shaderModule);
}

// 处理 Vulkan 调试回调。
VKAPI_ATTR VkBool32 VKAPI_CALL VKRender::DebugCallback(
    VkDebugUtilsMessageSeverityFlagBitsEXT severity,
    VkDebugUtilsMessageTypeFlagsEXT type,
    const VkDebugUtilsMessengerCallbackDataEXT* pCallbackData,
    void* pUserData) {
    std::cerr << "验证层: " << pCallbackData->pMessage << std::endl;
    return VK_FALSE;
}


// 读取着色器文件。
std::vector<char> VKRender::ReadShaderFile(const std::string& filename) {
    std::ifstream file(filename, std::ios::ate | std::ios::binary);

    if (!file.is_open()) {
        throw std::runtime_error("无法打开着色器文件: " + filename);
    }

    size_t fileSize = static_cast<size_t>(file.tellg());
    if (fileSize == 0 || (fileSize % 4) != 0) {
        throw std::runtime_error("着色器文件无效或不是 SPIR-V: " + filename);
    }
    std::vector<char> buffer(fileSize);

    file.seekg(0);
    file.read(buffer.data(), static_cast<std::streamsize>(fileSize));
    file.close();

    return buffer;
}

// 创建着色器模块，失败则抛错。
VkShaderModule VKRender::CreateShaderModuleHelper(const std::vector<char>& code,
                                                  const std::string& filename) {
    VkShaderModule shaderModule;
    if (CreateShaderModule(code, &shaderModule) != VK_SUCCESS) {
        throw std::runtime_error("创建着色器模块失败: " + filename);
    }
    return shaderModule;
}

// 把任务丢进线程池异步执行。
void VKRender::SubmitAsync(Render::AsyncTask task) {
    if (!task || IsShuttingDown() || IsDeviceLost()) return;
    if (m_asyncThreadPool) {
        m_asyncThreadPool->start(new VKFunctionRunnable(std::move(task)));
    }
}

// 把任务丢进线程池异步执行。
void VKRender::SubmitAsync(QRunnable* task) {
    if (!task || IsShuttingDown() || IsDeviceLost()) return;
    if (m_asyncThreadPool) {
        m_asyncThreadPool->start(task);
    }
}


// 返回 Vulkan 逻辑设备。
VkDevice VKRender::GetDevice() const { return m_device; }
// 返回 Vulkan 物理设备。
VkPhysicalDevice VKRender::GetPhysicalDevice() const { return m_physicalDevice; }

// 返回当前帧命令缓冲。
VkCommandBuffer VKRender::GetCurrentCommandBuffer() const { return m_commandBuffers[m_currentFrame]; }



// 按拓扑返回图形管线。
VkPipeline VKRender::GetPipeline(DrawTopology topology) const {
    if (topology >= 0 && topology < DT_COUNT) {
        return m_pipelines[topology];
    }
    return VK_NULL_HANDLE;
}


// 设置 Win32 表面（外部所有）。
void VKRender::SetSurface(VkSurfaceKHR surface) { m_surface = surface; }
// 设置外部 VkInstance。
void VKRender::SetInstance(VkInstance instance) { m_instance = instance; m_externalInstance = true; }

#include <iostream>
#include <cstring>
#include <glm/gtc/matrix_transform.hpp>

VKRender2D::VKRender2D() {}

VKRender2D::~VKRender2D() {}


// 处理鼠标按下。
void VKRender2D::OnMouseDown(float nx, float ny, int button) {
    m_mouseButton = button;
    m_lastMouse = glm::vec2(nx, ny);
}

// 处理鼠标移动。
void VKRender2D::OnMouseMove(float nx, float ny) {
    if (m_mouseButton < 0) return;
    glm::vec2 delta = glm::vec2(nx, ny) - m_lastMouse;
    m_lastMouse = glm::vec2(nx, ny);

    if (m_mouseButton == 0) {
        float aspect = (float)m_framebufferWidth / (float)m_framebufferHeight;
        float worldW = 2.0f * aspect * m_zoomLevel;
        float worldH = 2.0f * m_zoomLevel;
        m_panOffset.x -= delta.x * worldW;
        m_panOffset.y += delta.y * worldH;
    }
}

// 处理鼠标松开。
void VKRender2D::OnMouseUp(int /*button*/) {
    m_mouseButton = -1;
}

// 处理滚轮缩放。
void VKRender2D::OnMouseWheel(float delta) {
    m_zoomLevel *= (delta > 0.0f) ? 0.85f : 1.18f;
    m_zoomLevel = glm::clamp(m_zoomLevel, 0.01f, 100.0f);
}


// 后端初始化完成后的钩子。
bool VKRender2D::OnInitialize() {
    m_clearValueCount = 1;
    m_clearValues[0].color = { m_clearColor.x, m_clearColor.y, m_clearColor.z, m_clearColor.w };

    if (!CreateDescriptorSetLayout()) {
        if (m_lastError.empty()) SetLastError("2D: 创建描述符集布局失败");
        return false;
    }
    if (!CreateUniformBuffers()) {
        if (m_lastError.empty()) SetLastError("2D: 创建 UBO 失败");
        return false;
    }
    if (!CreateDescriptorPool()) {
        if (m_lastError.empty()) SetLastError("2D: 创建描述符池失败");
        return false;
    }
    if (!CreateDescriptorSets()) {
        if (m_lastError.empty()) SetLastError("2D: 创建描述符集失败");
        return false;
    }
    return true;
}

// 关闭前释放后端资源的钩子。
void VKRender2D::OnShutdown() {
    if (m_descriptorPool != VK_NULL_HANDLE) {
        vkDestroyDescriptorPool(m_device, m_descriptorPool, nullptr);
        m_descriptorPool = VK_NULL_HANDLE;
    }
    DestroyUniformBuffers();
    if (m_descriptorSetLayout != VK_NULL_HANDLE) {
        vkDestroyDescriptorSetLayout(m_device, m_descriptorSetLayout, nullptr);
        m_descriptorSetLayout = VK_NULL_HANDLE;
    }
}

// 每帧开始时的钩子。
void VKRender2D::OnBeginFrame() {
    UpdateCameraUBO();

    vkCmdBindDescriptorSets(m_commandBuffers[m_currentFrame], VK_PIPELINE_BIND_POINT_GRAPHICS,
        m_pipelineLayout, 0, 1, &m_descriptorSets[m_currentFrame], 0, nullptr);
}

// 每帧结束时的钩子。
void VKRender2D::OnEndFrame() {}


// 创建图形管线。
bool VKRender2D::CreatePipelines() {
    const std::string vertShaderPath = AssetPath::ShaderFile("2d_vert.spv");
    const std::string fragShaderPath = AssetPath::ShaderFile("2d_frag.spv");
    auto vertShaderCode = ReadShaderFile(vertShaderPath);
    auto fragShaderCode = ReadShaderFile(fragShaderPath);

    VkShaderModule vertShaderModule = CreateShaderModuleHelper(
        vertShaderCode, vertShaderPath);
    VkShaderModule fragShaderModule = CreateShaderModuleHelper(
        fragShaderCode, fragShaderPath);

    VkPipelineShaderStageCreateInfo vertShaderStageInfo{};
    vertShaderStageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    vertShaderStageInfo.stage = VK_SHADER_STAGE_VERTEX_BIT;
    vertShaderStageInfo.module = vertShaderModule;
    vertShaderStageInfo.pName = "main";

    VkPipelineShaderStageCreateInfo fragShaderStageInfo{};
    fragShaderStageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    fragShaderStageInfo.stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    fragShaderStageInfo.module = fragShaderModule;
    fragShaderStageInfo.pName = "main";

    VkPipelineShaderStageCreateInfo shaderStages[] = { vertShaderStageInfo, fragShaderStageInfo };

    auto bindingDescription = Vertex3D::GetBindingDescription();
    auto attributeDescriptions = Vertex3D::GetAttributeDescriptions();

    VkPipelineVertexInputStateCreateInfo vertexInputInfo{};
    vertexInputInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    vertexInputInfo.vertexBindingDescriptionCount = 1;
    vertexInputInfo.pVertexBindingDescriptions = &bindingDescription;
    vertexInputInfo.vertexAttributeDescriptionCount = static_cast<uint32_t>(attributeDescriptions.size());
    vertexInputInfo.pVertexAttributeDescriptions = attributeDescriptions.data();

    {
        VkPipelineLayoutCreateInfo pipelineLayoutInfo{};
        pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
        pipelineLayoutInfo.setLayoutCount = 1;
        pipelineLayoutInfo.pSetLayouts = &m_descriptorSetLayout;
        if (vkCreatePipelineLayout(m_device, &pipelineLayoutInfo, nullptr, &m_pipelineLayout) != VK_SUCCESS) {
            return Fail("2D: 创建管线布局失败");
        }
    }

    std::vector<VkDynamicState> dynamicStates = {
        VK_DYNAMIC_STATE_VIEWPORT,
        VK_DYNAMIC_STATE_SCISSOR
    };
    VkPipelineDynamicStateCreateInfo dynamicState{};
    dynamicState.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    dynamicState.dynamicStateCount = static_cast<uint32_t>(dynamicStates.size());
    dynamicState.pDynamicStates = dynamicStates.data();

    VkPipelineInputAssemblyStateCreateInfo inputAssembly{};
    inputAssembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    inputAssembly.primitiveRestartEnable = VK_FALSE;

    VkPipelineViewportStateCreateInfo viewportState{};
    viewportState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    viewportState.viewportCount = 1;
    viewportState.scissorCount = 1;

    VkPipelineRasterizationStateCreateInfo rasterizer{};
    rasterizer.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    rasterizer.depthClampEnable = VK_FALSE;
    rasterizer.rasterizerDiscardEnable = VK_FALSE;
    rasterizer.polygonMode = VK_POLYGON_MODE_FILL;
    rasterizer.lineWidth = 1.0f;
    rasterizer.cullMode = VK_CULL_MODE_BACK_BIT;
    rasterizer.frontFace = VK_FRONT_FACE_CLOCKWISE;
    rasterizer.depthBiasEnable = VK_FALSE;

    VkPipelineMultisampleStateCreateInfo multisampling{};
    multisampling.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    multisampling.sampleShadingEnable = VK_FALSE;
    multisampling.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

    VkPipelineColorBlendAttachmentState colorBlendAttachment{};
    colorBlendAttachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                                           VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    colorBlendAttachment.blendEnable = VK_FALSE;

    VkPipelineColorBlendStateCreateInfo colorBlending{};
    colorBlending.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    colorBlending.logicOpEnable = VK_FALSE;
    colorBlending.attachmentCount = 1;
    colorBlending.pAttachments = &colorBlendAttachment;

    VkGraphicsPipelineCreateInfo pipelineInfo{};
    pipelineInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    pipelineInfo.stageCount = 2;
    pipelineInfo.pStages = shaderStages;
    pipelineInfo.pVertexInputState = &vertexInputInfo;
    pipelineInfo.pInputAssemblyState = &inputAssembly;
    pipelineInfo.pViewportState = &viewportState;
    pipelineInfo.pRasterizationState = &rasterizer;
    pipelineInfo.pMultisampleState = &multisampling;
    pipelineInfo.pColorBlendState = &colorBlending;
    pipelineInfo.pDynamicState = &dynamicState;
    pipelineInfo.layout = m_pipelineLayout;
    pipelineInfo.renderPass = m_renderPass;
    pipelineInfo.subpass = 0;

    VkResult result = vkCreateGraphicsPipelines(m_device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &m_pipelines[DT_TRIANGLE]);
    if (result != VK_SUCCESS) {
        return Fail("2D: 创建图形管线失败");
    }

    vkDestroyShaderModule(m_device, fragShaderModule, nullptr);
    vkDestroyShaderModule(m_device, vertShaderModule, nullptr);

    return true;
}


// 创建描述符集布局。
bool VKRender2D::CreateDescriptorSetLayout() {
    VkDescriptorSetLayoutBinding uboLayoutBinding{};
    uboLayoutBinding.binding = 0;
    uboLayoutBinding.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    uboLayoutBinding.descriptorCount = 1;
    uboLayoutBinding.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
    uboLayoutBinding.pImmutableSamplers = nullptr;

    VkDescriptorSetLayoutCreateInfo layoutInfo{};
    layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    layoutInfo.bindingCount = 1;
    layoutInfo.pBindings = &uboLayoutBinding;

    if (vkCreateDescriptorSetLayout(m_device, &layoutInfo, nullptr, &m_descriptorSetLayout) != VK_SUCCESS) {
        std::cerr << "2D: 创建描述符集布局失败" << std::endl;
        return false;
    }
    return true;
}

// 创建并映射 UBO。
bool VKRender2D::CreateUniformBuffers() {
    VkDeviceSize bufferSize = sizeof(CameraUBO2D);

    m_uniformBuffers.resize(MAX_FRAMES_IN_FLIGHT);
    m_uniformBuffersMemory.resize(MAX_FRAMES_IN_FLIGHT);
    m_uniformBuffersMapped.resize(MAX_FRAMES_IN_FLIGHT);

    for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
        VkBufferCreateInfo bufferInfo{};
        bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        bufferInfo.size = bufferSize;
        bufferInfo.usage = VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT;
        bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

        if (vkCreateBuffer(m_device, &bufferInfo, nullptr, &m_uniformBuffers[i]) != VK_SUCCESS) {
            std::cerr << "2D: 创建统一缓冲区失败" << std::endl;
            return false;
        }

        VkMemoryRequirements memRequirements;
        vkGetBufferMemoryRequirements(m_device, m_uniformBuffers[i], &memRequirements);

        VkMemoryAllocateInfo allocInfo{};
        allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        allocInfo.allocationSize = memRequirements.size;
        allocInfo.memoryTypeIndex = FindMemoryType(memRequirements.memoryTypeBits,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);

        if (vkAllocateMemory(m_device, &allocInfo, nullptr, &m_uniformBuffersMemory[i]) != VK_SUCCESS) {
            std::cerr << "2D: 分配统一缓冲区内存失败" << std::endl;
            return false;
        }

        vkBindBufferMemory(m_device, m_uniformBuffers[i], m_uniformBuffersMemory[i], 0);

        vkMapMemory(m_device, m_uniformBuffersMemory[i], 0, bufferSize, 0, &m_uniformBuffersMapped[i]);

        CameraUBO2D ubo{};
        ubo.projView = glm::mat4(1.0f);
        memcpy(m_uniformBuffersMapped[i], &ubo, sizeof(ubo));
    }

    return true;
}

// 创建描述符池。
bool VKRender2D::CreateDescriptorPool() {
    VkDescriptorPoolSize poolSize{};
    poolSize.type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    poolSize.descriptorCount = static_cast<uint32_t>(MAX_FRAMES_IN_FLIGHT);

    VkDescriptorPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    poolInfo.poolSizeCount = 1;
    poolInfo.pPoolSizes = &poolSize;
    poolInfo.maxSets = static_cast<uint32_t>(MAX_FRAMES_IN_FLIGHT);

    if (vkCreateDescriptorPool(m_device, &poolInfo, nullptr, &m_descriptorPool) != VK_SUCCESS) {
        std::cerr << "2D: 创建描述符池失败" << std::endl;
        return false;
    }
    return true;
}

// 分配并写入描述符集。
bool VKRender2D::CreateDescriptorSets() {
    std::vector<VkDescriptorSetLayout> layouts(MAX_FRAMES_IN_FLIGHT, m_descriptorSetLayout);

    VkDescriptorSetAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    allocInfo.descriptorPool = m_descriptorPool;
    allocInfo.descriptorSetCount = static_cast<uint32_t>(MAX_FRAMES_IN_FLIGHT);
    allocInfo.pSetLayouts = layouts.data();

    m_descriptorSets.resize(MAX_FRAMES_IN_FLIGHT);
    if (vkAllocateDescriptorSets(m_device, &allocInfo, m_descriptorSets.data()) != VK_SUCCESS) {
        std::cerr << "2D: 分配描述符集失败" << std::endl;
        return false;
    }

    for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
        VkDescriptorBufferInfo bufferInfo{};
        bufferInfo.buffer = m_uniformBuffers[i];
        bufferInfo.offset = 0;
        bufferInfo.range = sizeof(CameraUBO2D);

        VkWriteDescriptorSet descriptorWrite{};
        descriptorWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        descriptorWrite.dstSet = m_descriptorSets[i];
        descriptorWrite.dstBinding = 0;
        descriptorWrite.dstArrayElement = 0;
        descriptorWrite.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        descriptorWrite.descriptorCount = 1;
        descriptorWrite.pBufferInfo = &bufferInfo;

        vkUpdateDescriptorSets(m_device, 1, &descriptorWrite, 0, nullptr);
    }

    return true;
}

// 销毁 UBO 及其内存。
void VKRender2D::DestroyUniformBuffers() {
    for (size_t i = 0; i < m_uniformBuffers.size(); i++) {
        if (m_uniformBuffersMapped[i] != nullptr) {
            vkUnmapMemory(m_device, m_uniformBuffersMemory[i]);
            m_uniformBuffersMapped[i] = nullptr;
        }
        if (m_uniformBuffers[i] != VK_NULL_HANDLE) {
            vkDestroyBuffer(m_device, m_uniformBuffers[i], nullptr);
            m_uniformBuffers[i] = VK_NULL_HANDLE;
        }
        if (m_uniformBuffersMemory[i] != VK_NULL_HANDLE) {
            vkFreeMemory(m_device, m_uniformBuffersMemory[i], nullptr);
            m_uniformBuffersMemory[i] = VK_NULL_HANDLE;
        }
    }
    m_uniformBuffers.clear();
    m_uniformBuffersMemory.clear();
    m_uniformBuffersMapped.clear();
}

// 写入二维相机 UBO。
void VKRender2D::UpdateCameraUBO() {
    float aspect = (float)m_framebufferWidth / (float)m_framebufferHeight;
    float halfW = aspect * m_zoomLevel;
    float halfH = m_zoomLevel;

    glm::mat4 proj = glm::ortho(-halfW, halfW, -halfH, halfH, -1.0f, 1.0f);
    proj[1][1] *= -1.0f;

    glm::mat4 view = glm::translate(glm::mat4(1.0f), glm::vec3(-m_panOffset.x, -m_panOffset.y, 0.0f));

    CameraUBO2D ubo;
    ubo.projView = proj * view;
    memcpy(m_uniformBuffersMapped[m_currentFrame], &ubo, sizeof(ubo));
}

#include "Light/SolarPosition.h"
#include <iostream>
#include <cstring>
#include <cmath>
#include <algorithm>
#include <limits>
#include <stdexcept>
#include <string>
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <glm/gtc/matrix_inverse.hpp>


VKRender3D::VKRender3D() {
    UpdateSunDirection();
}

VKRender3D::~VKRender3D() {
}



// 处理鼠标按下。
void VKRender3D::OnMouseDown(float nx, float ny, int button) {
    m_mouseButton = button;
    m_lastMouse = glm::vec2(nx, ny);
}

// 处理鼠标移动。
void VKRender3D::OnMouseMove(float nx, float ny) {
    if (m_mouseButton < 0) return;
    const glm::vec2 previousMouse = m_lastMouse;
    const glm::vec3 sphereFrom =
        ProjectToVirtualSphere(previousMouse.x, previousMouse.y);
    const glm::vec3 sphereTo = ProjectToVirtualSphere(nx, ny);
    m_lastMouse = glm::vec2(nx, ny);

    if (m_mouseButton == 0) {
        if (m_orthographicEnabled) {
            const float width = static_cast<float>(std::max(1u, m_framebufferWidth));
            const float height = static_cast<float>(std::max(1u, m_framebufferHeight));
            const float minExtent = std::min(width, height);
            const float horizontalDelta =
                (nx - previousMouse.x) * width / minExtent;
            if (std::abs(horizontalDelta) <=
                std::numeric_limits<float>::epsilon()) {
                return;
            }

            constexpr float rotationSensitivity = 4.71238898038f; // 270度
            ApplyConstrainedLocalRotation(
                glm::vec3(0.0f, 0.0f, 1.0f),
                horizontalDelta * rotationSensitivity);
            return;
        }

        const glm::vec3 sphereCross = glm::cross(sphereFrom, sphereTo);
        const float sinAngle = glm::length(sphereCross);
        if (sinAngle <= std::numeric_limits<float>::epsilon()) return;

        const float width = static_cast<float>(std::max(1u, m_framebufferWidth));
        const float height = static_cast<float>(std::max(1u, m_framebufferHeight));
        const float minExtent = std::min(width, height);
        const glm::vec2 screenDelta(
            (nx - previousMouse.x) * width / minExtent,
            (ny - previousMouse.y) * height / minExtent);
        constexpr float rotationSensitivity = 4.71238898038f; // 270度
        const float uniformAngle = glm::length(screenDelta) * rotationSensitivity;

        glm::vec2 rotationAxisXY(sphereCross.x, sphereCross.y);
        if (glm::length(rotationAxisXY) <= 1.0e-6f) {
            rotationAxisXY = glm::vec2(screenDelta.y, screenDelta.x);
        }
        rotationAxisXY = glm::normalize(rotationAxisXY);
        const glm::vec3 sphereRotation(
            rotationAxisXY.x * uniformAngle,
            rotationAxisXY.y * uniformAngle,
            0.0f);

        if (std::abs(sphereRotation.y) >
            std::numeric_limits<float>::epsilon()) {
            ApplyConstrainedLocalRotation(
                glm::vec3(0.0f, 0.0f, 1.0f), sphereRotation.y);
        }

        if (std::abs(sphereRotation.x) >
            std::numeric_limits<float>::epsilon()) {
            const glm::vec3 worldLocalX =
                m_modelRotation * glm::vec3(1.0f, 0.0f, 0.0f);
            const glm::vec3 worldLocalY =
                m_modelRotation * glm::vec3(0.0f, 1.0f, 0.0f);
            const glm::vec2 localWeights(worldLocalX.x, worldLocalY.x);

            glm::vec3 verticalLocalAxis = m_lastVerticalLocalAxis;
            if (glm::length(localWeights) > 1.0e-4f) {
                verticalLocalAxis = glm::normalize(
                    glm::vec3(localWeights.x, localWeights.y, 0.0f));
                m_lastVerticalLocalAxis = verticalLocalAxis;
            }

            ApplyConstrainedLocalRotation(verticalLocalAxis, sphereRotation.x);
        }
    } else if (m_mouseButton == 2) {
        const float width = static_cast<float>(std::max(1u, m_framebufferWidth));
        const float height = static_cast<float>(std::max(1u, m_framebufferHeight));
        const float aspect = width / height;
        const float visibleHeight = 2.0f * m_orbitDistance
            * std::tan(glm::radians(45.0f) * 0.5f);
        const float visibleWidth = visibleHeight * aspect;

        const glm::vec2 mouseDelta = glm::vec2(nx, ny) - previousMouse;
        m_panOffset.x += mouseDelta.x * visibleWidth;
        m_panOffset.y -= mouseDelta.y * visibleHeight;
    }
}

// 将鼠标位置映射到虚拟球面。
glm::vec3 VKRender3D::ProjectToVirtualSphere(float nx, float ny) const {
    const float width = static_cast<float>(std::max(1u, m_framebufferWidth));
    const float height = static_cast<float>(std::max(1u, m_framebufferHeight));
    const float minExtent = std::min(width, height);
    float x = (nx * 2.0f - 1.0f) * width / minExtent;
    float y = (1.0f - ny * 2.0f) * height / minExtent;

    const float distanceSquared = x * x + y * y;
    const float distance = std::sqrt(distanceSquared);
    constexpr float sphereToHyperbola = 0.70710678118f; // 1 / 平方根(2)

    float z;
    if (distance <= sphereToHyperbola) {
        z = std::sqrt(1.0f - distanceSquared);
    } else {
        z = 0.5f / distance;
    }

    return glm::normalize(glm::vec3(x, y, z));
}

// 应用受约束的局部旋转。
void VKRender3D::ApplyConstrainedLocalRotation(const glm::vec3& localAxis,
                                                    float angle) {
    if (std::abs(angle) <= std::numeric_limits<float>::epsilon()) return;

    auto rotationAt = [&](float ratio) {
        return glm::normalize(m_modelRotation * glm::angleAxis(angle * ratio, localAxis));
    };
    auto zAxisDoesNotPointDown = [](const glm::quat& rotation) {
        const glm::vec3 worldZ = rotation * glm::vec3(0.0f, 0.0f, 1.0f);
        return worldZ.y >= -1.0e-6f;
    };

    glm::quat candidate = rotationAt(1.0f);
    if (zAxisDoesNotPointDown(candidate)) {
        m_modelRotation = candidate;
        return;
    }

    float allowed = 0.0f;
    float rejected = 1.0f;
    for (int i = 0; i < 16; ++i) {
        const float middle = (allowed + rejected) * 0.5f;
        if (zAxisDoesNotPointDown(rotationAt(middle))) {
            allowed = middle;
        } else {
            rejected = middle;
        }
    }
    m_modelRotation = rotationAt(allowed);
}

// 处理鼠标松开。
void VKRender3D::OnMouseUp(int /*button*/) {
    m_mouseButton = -1;
}

// 处理滚轮缩放。
void VKRender3D::OnMouseWheel(float delta) {
    m_orbitDistance *= (delta > 0.0f) ? 0.9f : 1.1f;
    m_orbitDistance = glm::clamp(m_orbitDistance, 0.1f, 1000.0f);
}


// 后端初始化完成后的钩子。
bool VKRender3D::OnInitialize() {
    m_clearValueCount = 2;
    m_clearValues[0].color = { m_clearColor.x, m_clearColor.y, m_clearColor.z, m_clearColor.w };
    m_clearValues[1].depthStencil = { 1.0f, 0 };

    if (!CreateDescriptorSetLayout()) {
        if (m_lastError.empty()) SetLastError("3D: 创建描述符集布局失败");
        return false;
    }
    if (!CreateMaterialDescriptors()) {
        if (m_lastError.empty()) SetLastError("3D: 创建材质描述符失败");
        return false;
    }
    if (!CreateUniformBuffers()) {
        if (m_lastError.empty()) SetLastError("3D: 创建 UBO 失败");
        return false;
    }
    if (!CreateShadowSampler()) {
        if (m_lastError.empty()) SetLastError("3D: 创建阴影采样器失败");
        return false;
    }
    if (!CreateDummyShadowMap()) {
        if (m_lastError.empty()) SetLastError("3D: 创建占位阴影贴图失败");
        return false;
    }
    if (!CreateShadowRenderPass()) {
        if (m_lastError.empty()) SetLastError("3D: 创建阴影 RenderPass 失败");
        return false;
    }
    if (!CreateDescriptorPool()) {
        if (m_lastError.empty()) SetLastError("3D: 创建描述符池失败");
        return false;
    }
    if (!CreateDescriptorSets()) {
        if (m_lastError.empty()) SetLastError("3D: 创建描述符集失败");
        return false;
    }
    if (!CreateDepthReadbackResources()) {
        if (m_lastError.empty()) SetLastError("3D: 创建深度回读缓冲失败");
        return false;
    }
    return true;
}

// 关闭前释放后端资源的钩子。
void VKRender3D::OnShutdown() {
    DestroyHdrResources();
    DestroyHdrSupport();
    DestroyMsaaResources();
    DestroyMsaaSupport();
    DestroyDepthReadbackResources();
    DestroyShadowMap();
    DestroyDummyShadowMap();
    DestroyShadowSupport();
    DestroyDepthResources();
    if (m_descriptorPool != VK_NULL_HANDLE) {
        vkDestroyDescriptorPool(m_device, m_descriptorPool, nullptr);
        m_descriptorPool = VK_NULL_HANDLE;
    }
    DestroyMaterialDescriptors();
    DestroyUniformBuffers();
    if (m_descriptorSetLayout != VK_NULL_HANDLE) {
        vkDestroyDescriptorSetLayout(m_device, m_descriptorSetLayout, nullptr);
        m_descriptorSetLayout = VK_NULL_HANDLE;
    }
}

// 销毁图形管线的钩子。
void VKRender3D::OnDestroyPipelines() {
    if (m_shadowPipeline != VK_NULL_HANDLE) {
        vkDestroyPipeline(m_device, m_shadowPipeline, nullptr);
        m_shadowPipeline = VK_NULL_HANDLE;
    }
    // 1.6/1.7：HDR 与 MSAA 场景管线均与 m_pipelines 共用 m_pipelineLayout，
    // 必须在布局销毁前释放。
    for (int i = 0; i < DT_COUNT; ++i) {
        if (m_hdrPipelines[i] != VK_NULL_HANDLE) {
            vkDestroyPipeline(m_device, m_hdrPipelines[i], nullptr);
            m_hdrPipelines[i] = VK_NULL_HANDLE;
        }
        if (m_msaaLdrPipelines[i] != VK_NULL_HANDLE) {
            vkDestroyPipeline(m_device, m_msaaLdrPipelines[i], nullptr);
            m_msaaLdrPipelines[i] = VK_NULL_HANDLE;
        }
        if (m_msaaHdrPipelines[i] != VK_NULL_HANDLE) {
            vkDestroyPipeline(m_device, m_msaaHdrPipelines[i], nullptr);
            m_msaaHdrPipelines[i] = VK_NULL_HANDLE;
        }
    }
}

// 开始录制命令前的钩子。
void VKRender3D::OnPrepareFrame() {
    EnsureDummyShadowReady();
    // 命令池此时已可用，安全创建默认白/平面法线纹理（无纹理材质使用），
    // 并预分配其 set 1，避免绘制时 set 1 尚未绑定。
    if (EnsureDefaultWhiteTexture() != 0 && EnsureDefaultFlatNormalTexture() != 0) {
        MaterialDescriptorSetFor(MaterialTextureSet{});
    }
}

// 每帧开始时的钩子。
void VKRender3D::OnBeginFrame() {
    ProcessDepthReadback(m_currentFrame);
    UpdateUniformBuffer(m_currentFrame);

    vkCmdBindDescriptorSets(m_commandBuffers[m_currentFrame], VK_PIPELINE_BIND_POINT_GRAPHICS,
        m_pipelineLayout, 0, 1, &m_descriptorSets[m_currentFrame], 0, nullptr);
}

// 交换链/帧缓冲重建后的钩子。
void VKRender3D::OnRecreateSwapchain() {
    DestroyDepthResources();
}

// 交换链清理前释放 HDR 中间目标（尺寸相关资源）。
void VKRender3D::OnBeforeCleanupSwapchain() {
    DestroyHdrResources();
    DestroyMsaaResources();
}

// ---------------- HDR + 色调映射（1.6） ----------------

// 开启/关闭 HDR 路径。关闭时完全回退到旧的直接写交换链路径。
void VKRender3D::SetHdrEnabled(bool enabled) {
    m_hdrEnabled = enabled;
}

// 设置曝光（EV 档位）。
void VKRender3D::SetExposureEV(float ev) {
    m_exposureEV = ev;
}

// 1.7：阴影基础深度偏移。
void VKRender3D::SetShadowBias(float bias) {
    m_shadowBias = bias;
}

// 1.7：PCF 档位（0 关闭 / 1 = 3x3 / 2 = 5x5）。
void VKRender3D::SetShadowPcfMode(int mode) {
    m_shadowPcfMode = (mode < 0) ? 0 : (mode > 2 ? 2 : mode);
}

// 1.7：法线偏移的世界纹素倍数。
void VKRender3D::SetShadowNormalOffsetScale(float scale) {
    m_shadowNormalOffsetScale = (scale < 0.0f) ? 0.0f : scale;
}

// 1.7：半球环境光（线性空间）。
void VKRender3D::SetAmbientLight(const Vec3& skyColor, const Vec3& groundColor, float intensity) {
    m_ambientSkyColor = glm::vec3(skyColor.x, skyColor.y, skyColor.z);
    m_ambientGroundColor = glm::vec3(groundColor.x, groundColor.y, groundColor.z);
    m_ambientIntensity = (intensity < 0.0f) ? 0.0f : intensity;
}

// 1.7：调试视图（0 正常 / 1 深度 / 2 世界法线 / 3 阴影）。
void VKRender3D::SetDebugView(int view) {
    m_debugView = (view < 0) ? 0 : (view > 3 ? 3 : view);
}

// 1.7：开关 4x MSAA。
void VKRender3D::SetMsaaEnabled(bool enabled) {
    m_msaaEnabled = enabled;
}

// 按当前渲染目标返回管线：多重采样目标激活时优先返回 MSAA 管线，否则 HDR/LDR。
// 以“通道是否激活”为准（而非 UI 开关），避免目标创建失败回退时误用不匹配的管线。
VkPipeline VKRender3D::GetPipeline(DrawTopology topology) const {
    if (topology < 0 || topology >= DT_COUNT) return VK_NULL_HANDLE;
    if (m_msaaPassActive) {
        if (m_hdrPassActive && m_msaaHdrPipelines[topology] != VK_NULL_HANDLE) {
            return m_msaaHdrPipelines[topology];
        }
        if (m_msaaLdrPipelines[topology] != VK_NULL_HANDLE) {
            return m_msaaLdrPipelines[topology];
        }
    }
    if (m_hdrPassActive && m_hdrPipelines[topology] != VK_NULL_HANDLE) {
        return m_hdrPipelines[topology];
    }
    return m_pipelines[topology];
}

// 开始主颜色渲染通道：按 MSAA / HDR 开关绑定对应帧缓冲。
// 优先级：4x MSAA 目标 > HDR 中间目标 > 交换链主通道。
void VKRender3D::BeginColorRenderPass() {
    // HDR 颜色目标是 MSAA-HDR 的 resolve 目标，故 MSAA 或 HDR 任一开启时都需保证其存在。
    // 两者都关闭时完全走旧的直接写交换链路径，不分配 HDR 目标。
    const bool needHdrTarget = m_hdrEnabled || m_msaaEnabled;
    const bool hdrTargetReady = needHdrTarget
        ? (EnsureHdrTarget() && m_hdrFramebuffer != VK_NULL_HANDLE)
        : false;
    const bool wantHdr = m_hdrEnabled && hdrTargetReady;
    const bool wantMsaa = m_msaaEnabled && hdrTargetReady && EnsureMsaaTargets();

    if (wantMsaa) {
        VkFramebuffer framebuffer = VK_NULL_HANDLE;
        VkRenderPass renderPass = VK_NULL_HANDLE;
        if (wantHdr) {
            framebuffer = m_msaaHdrFramebuffer;
            renderPass = m_msaaHdrRenderPass;
        } else if (m_imageIndex < m_msaaLdrFramebuffers.size()) {
            framebuffer = m_msaaLdrFramebuffers[m_imageIndex];
            renderPass = m_msaaLdrRenderPass;
        }
        if (framebuffer != VK_NULL_HANDLE && renderPass != VK_NULL_HANDLE) {
            m_msaaPassActive = true;
            m_hdrPassActive = wantHdr;
            OnBeginFrame();

            VkRenderPassBeginInfo renderPassInfo{};
            renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
            renderPassInfo.renderPass = renderPass;
            renderPassInfo.framebuffer = framebuffer;
            renderPassInfo.renderArea.offset = { 0, 0 };
            renderPassInfo.renderArea.extent = m_swapchainExtent;
            renderPassInfo.clearValueCount = m_clearValueCount;
            renderPassInfo.pClearValues = m_clearValues;
            vkCmdBeginRenderPass(m_commandBuffers[m_currentFrame], &renderPassInfo, VK_SUBPASS_CONTENTS_INLINE);

            VkViewport viewport{};
            viewport.width = static_cast<float>(m_swapchainExtent.width);
            viewport.height = static_cast<float>(m_swapchainExtent.height);
            viewport.minDepth = 0.0f;
            viewport.maxDepth = 1.0f;
            vkCmdSetViewport(m_commandBuffers[m_currentFrame], 0, 1, &viewport);

            VkRect2D scissor{};
            scissor.extent = m_swapchainExtent;
            vkCmdSetScissor(m_commandBuffers[m_currentFrame], 0, 1, &scissor);

            m_colorPassOpen = true;
            return;
        }
    }

    m_msaaPassActive = false;

    if (wantHdr) {
        m_hdrPassActive = true;
        OnBeginFrame();

        VkRenderPassBeginInfo renderPassInfo{};
        renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
        renderPassInfo.renderPass = m_hdrRenderPass;
        renderPassInfo.framebuffer = m_hdrFramebuffer;
        renderPassInfo.renderArea.offset = { 0, 0 };
        renderPassInfo.renderArea.extent = m_swapchainExtent;
        renderPassInfo.clearValueCount = m_clearValueCount;
        renderPassInfo.pClearValues = m_clearValues;
        vkCmdBeginRenderPass(m_commandBuffers[m_currentFrame], &renderPassInfo, VK_SUBPASS_CONTENTS_INLINE);

        VkViewport viewport{};
        viewport.x = 0.0f;
        viewport.y = 0.0f;
        viewport.width = static_cast<float>(m_swapchainExtent.width);
        viewport.height = static_cast<float>(m_swapchainExtent.height);
        viewport.minDepth = 0.0f;
        viewport.maxDepth = 1.0f;
        vkCmdSetViewport(m_commandBuffers[m_currentFrame], 0, 1, &viewport);

        VkRect2D scissor{};
        scissor.offset = { 0, 0 };
        scissor.extent = m_swapchainExtent;
        vkCmdSetScissor(m_commandBuffers[m_currentFrame], 0, 1, &scissor);

        m_colorPassOpen = true;
        return;
    }

    m_hdrPassActive = false;
    VKRender::BeginColorRenderPass();
}

// 场景通道结束后执行 HDR 后处理：HDR 目标 -> 曝光 + ACES -> 交换链。
// LDR 路径无需后处理（MSAA 时场景通道已把多重采样颜色 resolve 到交换链图像）。
void VKRender3D::OnAfterScenePass() {
    const bool hdr = m_hdrPassActive;
    m_hdrPassActive = false;

    if (!hdr) return;

    // 结束写入 HDR 中间目标的场景通道（MSAA 时 resolve 到 m_hdrColorImage）。
    EndColorRenderPass();

    if (!m_postPipelineReady || m_postPipeline == VK_NULL_HANDLE ||
        m_hdrColorView == VK_NULL_HANDLE || m_postDescriptorSet == VK_NULL_HANDLE ||
        m_postRenderPass == VK_NULL_HANDLE || m_imageIndex >= m_postFramebuffers.size() ||
        m_postFramebuffers[m_imageIndex] == VK_NULL_HANDLE) {
        return;
    }

    VkCommandBuffer cmd = m_commandBuffers[m_currentFrame];

    // 写入交换链的后处理通道（仅颜色附件，不触碰场景深度）。
    VkRenderPassBeginInfo renderPassInfo{};
    renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    renderPassInfo.renderPass = m_postRenderPass;
    renderPassInfo.framebuffer = m_postFramebuffers[m_imageIndex];
    renderPassInfo.renderArea.offset = { 0, 0 };
    renderPassInfo.renderArea.extent = m_swapchainExtent;
    renderPassInfo.clearValueCount = 0;
    renderPassInfo.pClearValues = nullptr;
    vkCmdBeginRenderPass(cmd, &renderPassInfo, VK_SUBPASS_CONTENTS_INLINE);

    VkViewport viewport{};
    viewport.x = 0.0f;
    viewport.y = 0.0f;
    viewport.width = static_cast<float>(m_swapchainExtent.width);
    viewport.height = static_cast<float>(m_swapchainExtent.height);
    viewport.minDepth = 0.0f;
    viewport.maxDepth = 1.0f;
    vkCmdSetViewport(cmd, 0, 1, &viewport);

    VkRect2D scissor{};
    scissor.offset = { 0, 0 };
    scissor.extent = m_swapchainExtent;
    vkCmdSetScissor(cmd, 0, 1, &scissor);

    // 曝光乘数 = 2^EV；调试视图时置位调试标志（后处理跳过曝光与色调映射）。
    struct PostParams { float params[4]; } push{};
    push.params[0] = std::pow(2.0f, m_exposureEV);
    push.params[1] = (m_debugView != 0) ? 1.0f : 0.0f;
    push.params[2] = 0.0f;
    push.params[3] = 0.0f;

    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_postPipeline);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_postPipelineLayout,
                            0, 1, &m_postDescriptorSet, 0, nullptr);
    vkCmdPushConstants(cmd, m_postPipelineLayout, VK_SHADER_STAGE_FRAGMENT_BIT,
                       0, sizeof(push), &push);
    vkCmdDraw(cmd, 3, 1, 0, 0);

    vkCmdEndRenderPass(cmd);
    m_colorPassOpen = false;
}

// 确保 HDR 中间目标与当前交换链尺寸一致；必要时创建。
bool VKRender3D::EnsureHdrTarget() {
    if (m_hdrFramebuffer != VK_NULL_HANDLE && m_hdrColorImage != VK_NULL_HANDLE) {
        return true;
    }
    try {
        if (!CreateHdrRenderPass()) return false;
        if (!CreateHdrResources()) return false;
        if (!CreatePostPipeline()) return false;
    } catch (const std::exception& e) {
        // 设备不支持所需 HDR 格式等情况：本次回退到旧的 LDR 直出路径，避免崩溃。
        SetLastError(e.what());
        return false;
    }
    // 资源（重新）创建后刷新后处理描述符绑定（交换链重建会替换 HDR 视图）。
    UpdatePostDescriptors();
    return true;
}

// 创建 HDR 渲染通道（颜色附件 + 深度附件，最终布局可被采样）。
bool VKRender3D::CreateHdrRenderPass() {
    if (m_hdrRenderPass != VK_NULL_HANDLE) return true;

    // 优先 16F，退回 32F；两者都是线性高精度格式。
    m_hdrFormat = FindSupportedFormat(
        { VK_FORMAT_R16G16B16A16_SFLOAT, VK_FORMAT_R32G32B32A32_SFLOAT },
        VK_IMAGE_TILING_OPTIMAL,
        VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BIT | VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT);

    VkAttachmentDescription colorAttachment{};
    colorAttachment.format = m_hdrFormat;
    colorAttachment.samples = VK_SAMPLE_COUNT_1_BIT;
    colorAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    colorAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    colorAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    colorAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    colorAttachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    colorAttachment.finalLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

    VkAttachmentReference colorRef{};
    colorRef.attachment = 0;
    colorRef.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

    VkAttachmentDescription depthAttachment{};
    depthAttachment.format = FindDepthFormat();
    depthAttachment.samples = VK_SAMPLE_COUNT_1_BIT;
    depthAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    depthAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    depthAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    depthAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    depthAttachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    depthAttachment.finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

    VkAttachmentReference depthRef{};
    depthRef.attachment = 1;
    depthRef.layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

    VkSubpassDescription subpass{};
    subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.colorAttachmentCount = 1;
    subpass.pColorAttachments = &colorRef;
    subpass.pDepthStencilAttachment = &depthRef;

    VkSubpassDependency dependencies[2]{};
    dependencies[0].srcSubpass = VK_SUBPASS_EXTERNAL;
    dependencies[0].dstSubpass = 0;
    dependencies[0].srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT |
                                   VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
    dependencies[0].srcAccessMask = 0;
    dependencies[0].dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT |
                                    VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
    dependencies[0].dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT |
                                     VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
    // 出向依赖：把 HDR 颜色写入转为着色器可读（供后续后处理片元采样）。
    dependencies[1].srcSubpass = 0;
    dependencies[1].dstSubpass = VK_SUBPASS_EXTERNAL;
    dependencies[1].srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    dependencies[1].srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    dependencies[1].dstStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
    dependencies[1].dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

    std::vector<VkAttachmentDescription> attachments = { colorAttachment, depthAttachment };
    VkRenderPassCreateInfo renderPassInfo{};
    renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    renderPassInfo.attachmentCount = static_cast<uint32_t>(attachments.size());
    renderPassInfo.pAttachments = attachments.data();
    renderPassInfo.subpassCount = 1;
    renderPassInfo.pSubpasses = &subpass;
    renderPassInfo.dependencyCount = 2;
    renderPassInfo.pDependencies = dependencies;

    if (vkCreateRenderPass(m_device, &renderPassInfo, nullptr, &m_hdrRenderPass) != VK_SUCCESS) {
        return Fail("3D: 创建 HDR RenderPass 失败");
    }
    return true;
}

// 创建 HDR 颜色图像、视图与帧缓冲（尺寸随交换链）。
bool VKRender3D::CreateHdrResources() {
    if (m_hdrFormat == VK_FORMAT_UNDEFINED) return false;
    // 深度图像由主交换链帧缓冲路径（CreateFramebuffers -> CreateDepthResources）创建，
    // 这里复用同一张 m_depthImageView，避免重复分配。
    if (m_depthImageView == VK_NULL_HANDLE) return false;

    VkImageCreateInfo imageInfo{};
    imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    imageInfo.imageType = VK_IMAGE_TYPE_2D;
    imageInfo.extent.width = m_swapchainExtent.width;
    imageInfo.extent.height = m_swapchainExtent.height;
    imageInfo.extent.depth = 1;
    imageInfo.mipLevels = 1;
    imageInfo.arrayLayers = 1;
    imageInfo.format = m_hdrFormat;
    imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
    imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    imageInfo.usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
    imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    if (vkCreateImage(m_device, &imageInfo, nullptr, &m_hdrColorImage) != VK_SUCCESS) {
        return Fail("3D: 创建 HDR 颜色图像失败");
    }

    VkMemoryRequirements memRequirements;
    vkGetImageMemoryRequirements(m_device, m_hdrColorImage, &memRequirements);
    VkMemoryAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocInfo.allocationSize = memRequirements.size;
    try {
        allocInfo.memoryTypeIndex = FindMemoryType(memRequirements.memoryTypeBits,
            VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    } catch (...) {
        DestroyHdrResources();
        return false;
    }
    if (vkAllocateMemory(m_device, &allocInfo, nullptr, &m_hdrColorMemory) != VK_SUCCESS) {
        DestroyHdrResources();
        return Fail("3D: 分配 HDR 颜色图像内存失败");
    }
    vkBindImageMemory(m_device, m_hdrColorImage, m_hdrColorMemory, 0);

    VkImageViewCreateInfo viewInfo{};
    viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    viewInfo.image = m_hdrColorImage;
    viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
    viewInfo.format = m_hdrFormat;
    viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    viewInfo.subresourceRange.baseMipLevel = 0;
    viewInfo.subresourceRange.levelCount = 1;
    viewInfo.subresourceRange.baseArrayLayer = 0;
    viewInfo.subresourceRange.layerCount = 1;
    if (vkCreateImageView(m_device, &viewInfo, nullptr, &m_hdrColorView) != VK_SUCCESS) {
        DestroyHdrResources();
        return Fail("3D: 创建 HDR 颜色图像视图失败");
    }

    VkImageView attachments[] = { m_hdrColorView, m_depthImageView };
    VkFramebufferCreateInfo framebufferInfo{};
    framebufferInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
    framebufferInfo.renderPass = m_hdrRenderPass;
    framebufferInfo.attachmentCount = 2;
    framebufferInfo.pAttachments = attachments;
    framebufferInfo.width = m_swapchainExtent.width;
    framebufferInfo.height = m_swapchainExtent.height;
    framebufferInfo.layers = 1;
    if (vkCreateFramebuffer(m_device, &framebufferInfo, nullptr, &m_hdrFramebuffer) != VK_SUCCESS) {
        DestroyHdrResources();
        return Fail("3D: 创建 HDR 帧缓冲失败");
    }

    // 后处理写入交换链的帧缓冲（每个交换链图像一个，仅颜色附件）。
    if (!CreatePostRenderPass()) {
        DestroyHdrResources();
        return false;
    }
    m_postFramebuffers.assign(m_swapchainImageViews.size(), VK_NULL_HANDLE);
    for (size_t i = 0; i < m_swapchainImageViews.size(); ++i) {
        VkImageView colorAttachment = m_swapchainImageViews[i];
        VkFramebufferCreateInfo postFbInfo{};
        postFbInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
        postFbInfo.renderPass = m_postRenderPass;
        postFbInfo.attachmentCount = 1;
        postFbInfo.pAttachments = &colorAttachment;
        postFbInfo.width = m_swapchainExtent.width;
        postFbInfo.height = m_swapchainExtent.height;
        postFbInfo.layers = 1;
        if (vkCreateFramebuffer(m_device, &postFbInfo, nullptr, &m_postFramebuffers[i]) != VK_SUCCESS) {
            DestroyHdrResources();
            return Fail("3D: 创建后处理帧缓冲失败");
        }
    }
    return true;
}

// 创建后处理渲染通道：仅颜色附件，最终布局为 PRESENT_SRC（可直接呈现）。
bool VKRender3D::CreatePostRenderPass() {
    if (m_postRenderPass != VK_NULL_HANDLE) return true;

    VkAttachmentDescription colorAttachment{};
    colorAttachment.format = m_swapchainImageFormat;
    colorAttachment.samples = VK_SAMPLE_COUNT_1_BIT;
    colorAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;   // 全屏覆盖，无需加载旧内容
    colorAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    colorAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    colorAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    colorAttachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    colorAttachment.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

    VkAttachmentReference colorRef{};
    colorRef.attachment = 0;
    colorRef.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

    VkSubpassDescription subpass{};
    subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.colorAttachmentCount = 1;
    subpass.pColorAttachments = &colorRef;

    VkSubpassDependency dependency{};
    dependency.srcSubpass = VK_SUBPASS_EXTERNAL;
    dependency.dstSubpass = 0;
    dependency.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    dependency.srcAccessMask = 0;
    dependency.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    dependency.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;

    VkRenderPassCreateInfo renderPassInfo{};
    renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    renderPassInfo.attachmentCount = 1;
    renderPassInfo.pAttachments = &colorAttachment;
    renderPassInfo.subpassCount = 1;
    renderPassInfo.pSubpasses = &subpass;
    renderPassInfo.dependencyCount = 1;
    renderPassInfo.pDependencies = &dependency;

    if (vkCreateRenderPass(m_device, &renderPassInfo, nullptr, &m_postRenderPass) != VK_SUCCESS) {
        return Fail("3D: 创建后处理 RenderPass 失败");
    }
    return true;
}

// 创建 HDR 后处理管线（全屏三角形 + 曝光 + ACES）。
bool VKRender3D::CreatePostPipeline() {
    if (m_postPipelineReady) return true;

    // 采样器。
    if (m_postSampler == VK_NULL_HANDLE) {
        VkSamplerCreateInfo samplerInfo{};
        samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
        samplerInfo.magFilter = VK_FILTER_LINEAR;
        samplerInfo.minFilter = VK_FILTER_LINEAR;
        samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
        samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        samplerInfo.maxAnisotropy = 1.0f;
        if (vkCreateSampler(m_device, &samplerInfo, nullptr, &m_postSampler) != VK_SUCCESS) {
            return Fail("3D: 创建后处理采样器失败");
        }
    }

    // 描述符集布局：binding 0 = HDR 颜色纹理。
    if (m_postSetLayout == VK_NULL_HANDLE) {
        VkDescriptorSetLayoutBinding binding{};
        binding.binding = 0;
        binding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        binding.descriptorCount = 1;
        binding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
        VkDescriptorSetLayoutCreateInfo layoutInfo{};
        layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
        layoutInfo.bindingCount = 1;
        layoutInfo.pBindings = &binding;
        if (vkCreateDescriptorSetLayout(m_device, &layoutInfo, nullptr, &m_postSetLayout) != VK_SUCCESS) {
            return Fail("3D: 创建后处理描述符集布局失败");
        }
    }

    if (m_postDescriptorPool == VK_NULL_HANDLE) {
        VkDescriptorPoolSize poolSize{};
        poolSize.type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        poolSize.descriptorCount = 1;
        VkDescriptorPoolCreateInfo poolInfo{};
        poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
        poolInfo.poolSizeCount = 1;
        poolInfo.pPoolSizes = &poolSize;
        poolInfo.maxSets = 1;
        if (vkCreateDescriptorPool(m_device, &poolInfo, nullptr, &m_postDescriptorPool) != VK_SUCCESS) {
            return Fail("3D: 创建后处理描述符池失败");
        }
    }

    if (m_postDescriptorSet == VK_NULL_HANDLE) {
        VkDescriptorSetAllocateInfo allocInfo{};
        allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
        allocInfo.descriptorPool = m_postDescriptorPool;
        allocInfo.descriptorSetCount = 1;
        allocInfo.pSetLayouts = &m_postSetLayout;
        if (vkAllocateDescriptorSets(m_device, &allocInfo, &m_postDescriptorSet) != VK_SUCCESS) {
            return Fail("3D: 分配后处理描述符集失败");
        }
    }
    UpdatePostDescriptors();

    // 管线布局：push constant（后处理参数）。
    if (m_postPipelineLayout == VK_NULL_HANDLE) {
        VkPushConstantRange pushRange{};
        pushRange.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
        pushRange.offset = 0;
        pushRange.size = sizeof(float) * 4;
        VkPipelineLayoutCreateInfo layoutInfo{};
        layoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
        layoutInfo.setLayoutCount = 1;
        layoutInfo.pSetLayouts = &m_postSetLayout;
        layoutInfo.pushConstantRangeCount = 1;
        layoutInfo.pPushConstantRanges = &pushRange;
        if (vkCreatePipelineLayout(m_device, &layoutInfo, nullptr, &m_postPipelineLayout) != VK_SUCCESS) {
            return Fail("3D: 创建后处理管线布局失败");
        }
    }

    // 着色器。
    const std::string vertPath = AssetPath::ShaderFile("post_vert.spv");
    const std::string fragPath = AssetPath::ShaderFile("post_frag.spv");
    VkShaderModule vertModule = CreateShaderModuleHelper(ReadShaderFile(vertPath), vertPath);
    VkShaderModule fragModule = CreateShaderModuleHelper(ReadShaderFile(fragPath), fragPath);

    VkPipelineShaderStageCreateInfo stages[2]{};
    stages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
    stages[0].module = vertModule;
    stages[0].pName = "main";
    stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    stages[1].module = fragModule;
    stages[1].pName = "main";

    VkPipelineVertexInputStateCreateInfo vertexInput{};
    vertexInput.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;

    VkPipelineInputAssemblyStateCreateInfo inputAssembly{};
    inputAssembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

    std::vector<VkDynamicState> dynamicStates = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };
    VkPipelineDynamicStateCreateInfo dynamicState{};
    dynamicState.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    dynamicState.dynamicStateCount = static_cast<uint32_t>(dynamicStates.size());
    dynamicState.pDynamicStates = dynamicStates.data();

    VkPipelineViewportStateCreateInfo viewportState{};
    viewportState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    viewportState.viewportCount = 1;
    viewportState.scissorCount = 1;

    VkPipelineRasterizationStateCreateInfo rasterizer{};
    rasterizer.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    rasterizer.polygonMode = VK_POLYGON_MODE_FILL;
    rasterizer.lineWidth = 1.0f;
    rasterizer.cullMode = VK_CULL_MODE_NONE;
    rasterizer.frontFace = VK_FRONT_FACE_CLOCKWISE;

    VkPipelineMultisampleStateCreateInfo multisampling{};
    multisampling.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    multisampling.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

    VkPipelineDepthStencilStateCreateInfo depthStencil{};
    depthStencil.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    depthStencil.depthTestEnable = VK_FALSE;
    depthStencil.depthWriteEnable = VK_FALSE;

    VkPipelineColorBlendAttachmentState colorBlendAttachment{};
    colorBlendAttachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                                           VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    colorBlendAttachment.blendEnable = VK_FALSE;
    VkPipelineColorBlendStateCreateInfo colorBlending{};
    colorBlending.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    colorBlending.attachmentCount = 1;
    colorBlending.pAttachments = &colorBlendAttachment;

    VkGraphicsPipelineCreateInfo pipelineInfo{};
    pipelineInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    pipelineInfo.stageCount = 2;
    pipelineInfo.pStages = stages;
    pipelineInfo.pVertexInputState = &vertexInput;
    pipelineInfo.pInputAssemblyState = &inputAssembly;
    pipelineInfo.pViewportState = &viewportState;
    pipelineInfo.pRasterizationState = &rasterizer;
    pipelineInfo.pMultisampleState = &multisampling;
    pipelineInfo.pDepthStencilState = &depthStencil;
    pipelineInfo.pColorBlendState = &colorBlending;
    pipelineInfo.pDynamicState = &dynamicState;
    pipelineInfo.layout = m_postPipelineLayout;
    pipelineInfo.renderPass = m_postRenderPass;  // 后处理专用通道（仅颜色附件）
    pipelineInfo.subpass = 0;

    VkResult result = vkCreateGraphicsPipelines(m_device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &m_postPipeline);
    vkDestroyShaderModule(m_device, fragModule, nullptr);
    vkDestroyShaderModule(m_device, vertModule, nullptr);
    if (result != VK_SUCCESS) {
        return Fail("3D: 创建后处理管线失败");
    }

    m_postPipelineReady = true;
    return true;
}

// 写入后处理描述符集（HDR 视图 + 采样器）。
void VKRender3D::UpdatePostDescriptors() {
    if (m_device == VK_NULL_HANDLE || m_postDescriptorSet == VK_NULL_HANDLE) return;
    if (m_postSampler == VK_NULL_HANDLE || m_hdrColorView == VK_NULL_HANDLE) return;

    VkDescriptorImageInfo imageInfo{};
    imageInfo.sampler = m_postSampler;
    imageInfo.imageView = m_hdrColorView;
    imageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

    VkWriteDescriptorSet write{};
    write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    write.dstSet = m_postDescriptorSet;
    write.dstBinding = 0;
    write.dstArrayElement = 0;
    write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    write.descriptorCount = 1;
    write.pImageInfo = &imageInfo;
    vkUpdateDescriptorSets(m_device, 1, &write, 0, nullptr);
}

// 销毁 HDR 中间目标（图像/视图/帧缓冲 + 后处理帧缓冲）。
void VKRender3D::DestroyHdrResources() {
    if (m_device == VK_NULL_HANDLE) {
        m_postFramebuffers.clear();
        m_hdrFramebuffer = VK_NULL_HANDLE;
        m_hdrColorView = VK_NULL_HANDLE;
        m_hdrColorMemory = VK_NULL_HANDLE;
        m_hdrColorImage = VK_NULL_HANDLE;
        return;
    }
    for (VkFramebuffer fb : m_postFramebuffers) {
        if (fb != VK_NULL_HANDLE) {
            vkDestroyFramebuffer(m_device, fb, nullptr);
        }
    }
    m_postFramebuffers.clear();
    if (m_hdrFramebuffer != VK_NULL_HANDLE) {
        vkDestroyFramebuffer(m_device, m_hdrFramebuffer, nullptr);
        m_hdrFramebuffer = VK_NULL_HANDLE;
    }
    if (m_hdrColorView != VK_NULL_HANDLE) {
        vkDestroyImageView(m_device, m_hdrColorView, nullptr);
        m_hdrColorView = VK_NULL_HANDLE;
    }
    if (m_hdrColorMemory != VK_NULL_HANDLE) {
        vkFreeMemory(m_device, m_hdrColorMemory, nullptr);
        m_hdrColorMemory = VK_NULL_HANDLE;
    }
    if (m_hdrColorImage != VK_NULL_HANDLE) {
        vkDestroyImage(m_device, m_hdrColorImage, nullptr);
        m_hdrColorImage = VK_NULL_HANDLE;
    }
}

// 销毁 HDR 渲染通道、管线、描述符与采样器（非尺寸相关）。
void VKRender3D::DestroyHdrSupport() {
    if (m_device != VK_NULL_HANDLE) {
        // HDR 场景管线已在 OnDestroyPipelines 中随 m_pipelineLayout 一起释放。
        if (m_postPipeline != VK_NULL_HANDLE) {
            vkDestroyPipeline(m_device, m_postPipeline, nullptr);
            m_postPipeline = VK_NULL_HANDLE;
        }
        if (m_postPipelineLayout != VK_NULL_HANDLE) {
            vkDestroyPipelineLayout(m_device, m_postPipelineLayout, nullptr);
            m_postPipelineLayout = VK_NULL_HANDLE;
        }
        if (m_postDescriptorPool != VK_NULL_HANDLE) {
            vkDestroyDescriptorPool(m_device, m_postDescriptorPool, nullptr);
            m_postDescriptorPool = VK_NULL_HANDLE;
        }
        if (m_postSetLayout != VK_NULL_HANDLE) {
            vkDestroyDescriptorSetLayout(m_device, m_postSetLayout, nullptr);
            m_postSetLayout = VK_NULL_HANDLE;
        }
        if (m_postSampler != VK_NULL_HANDLE) {
            vkDestroySampler(m_device, m_postSampler, nullptr);
            m_postSampler = VK_NULL_HANDLE;
        }
        if (m_hdrRenderPass != VK_NULL_HANDLE) {
            vkDestroyRenderPass(m_device, m_hdrRenderPass, nullptr);
            m_hdrRenderPass = VK_NULL_HANDLE;
        }
        if (m_postRenderPass != VK_NULL_HANDLE) {
            vkDestroyRenderPass(m_device, m_postRenderPass, nullptr);
            m_postRenderPass = VK_NULL_HANDLE;
        }
    }
    m_postDescriptorSet = VK_NULL_HANDLE;
    m_postPipelineReady = false;
}


// ---------------- 1.7 4x MSAA + 深度解析 ----------------

// 确保 4x MSAA 中间目标与当前尺寸/模式匹配；必要时创建。
bool VKRender3D::EnsureMsaaTargets() {
    if (!m_msaaEnabled) return false;
    if (m_msaaTargetsReady && m_msaaSupportReady) return true;
    try {
        if (!CreateMsaaRenderPasses()) return false;
        if (!CreateDepthResolveSupport()) return false;
        if (!CreateMsaaColorDepthResources()) return false;
        if (!CreateMsaaPipelines()) return false;
    } catch (const std::exception& e) {
        SetLastError(e.what());
        return false;
    }
    m_msaaTargetsReady = true;
    m_msaaSupportReady = true;
    UpdateDepthResolveDescriptors();
    return true;
}

// 创建 LDR / HDR 两个多重采样渲染通道（颜色为多重采样并在子通道中 resolve）。
bool VKRender3D::CreateMsaaRenderPasses() {
    if (m_msaaLdrRenderPass != VK_NULL_HANDLE && m_msaaHdrRenderPass != VK_NULL_HANDLE) {
        return true;
    }
    m_msaaSampleCount = VK_SAMPLE_COUNT_4_BIT;
    m_msaaDepthFormat = FindSupportedFormat(
        { VK_FORMAT_D32_SFLOAT, VK_FORMAT_D32_SFLOAT_S8_UINT, VK_FORMAT_D24_UNORM_S8_UINT },
        VK_IMAGE_TILING_OPTIMAL,
        VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT);

    // 颜色附件（多重采样，子通道 resolve 到单采样目标）。
    VkAttachmentDescription msaaColor{};
    msaaColor.format = m_swapchainImageFormat;
    msaaColor.samples = m_msaaSampleCount;
    msaaColor.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    msaaColor.storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;  // 仅用于 resolve
    msaaColor.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    msaaColor.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    msaaColor.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    msaaColor.finalLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

    // 多重采样深度附件（也用于深度解析）。
    VkAttachmentDescription msaaDepth{};
    msaaDepth.format = m_msaaDepthFormat;
    msaaDepth.samples = m_msaaSampleCount;
    msaaDepth.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    msaaDepth.storeOp = VK_ATTACHMENT_STORE_OP_STORE;  // 深度解析需要保留
    msaaDepth.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    msaaDepth.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    msaaDepth.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    msaaDepth.finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

    VkAttachmentReference msaaColorRef{};
    msaaColorRef.attachment = 0;
    msaaColorRef.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    VkAttachmentReference msaaDepthRef{};
    msaaDepthRef.attachment = 1;
    msaaDepthRef.layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

    // LDR：resolve 到交换链图像（附件 2），finalLayout=PRESENT_SRC。
    {
        VkAttachmentDescription resolveAttachment{};
        resolveAttachment.format = m_swapchainImageFormat;
        resolveAttachment.samples = VK_SAMPLE_COUNT_1_BIT;
        resolveAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        resolveAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        resolveAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        resolveAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        resolveAttachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        resolveAttachment.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

        VkAttachmentReference resolveRef{};
        resolveRef.attachment = 2;
        resolveRef.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

        VkSubpassDescription subpass{};
        subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
        subpass.colorAttachmentCount = 1;
        subpass.pColorAttachments = &msaaColorRef;
        subpass.pResolveAttachments = &resolveRef;
        subpass.pDepthStencilAttachment = &msaaDepthRef;

        VkSubpassDependency dependency{};
        dependency.srcSubpass = VK_SUBPASS_EXTERNAL;
        dependency.dstSubpass = 0;
        dependency.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT |
                                  VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
        dependency.srcAccessMask = 0;
        dependency.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT |
                                   VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
        dependency.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT |
                                    VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;

        std::vector<VkAttachmentDescription> attachments = { msaaColor, msaaDepth, resolveAttachment };
        VkRenderPassCreateInfo info{};
        info.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
        info.attachmentCount = static_cast<uint32_t>(attachments.size());
        info.pAttachments = attachments.data();
        info.subpassCount = 1;
        info.pSubpasses = &subpass;
        info.dependencyCount = 1;
        info.pDependencies = &dependency;
        if (vkCreateRenderPass(m_device, &info, nullptr, &m_msaaLdrRenderPass) != VK_SUCCESS) {
            return Fail("3D: 创建 MSAA LDR RenderPass 失败");
        }
    }

    // HDR：resolve 到 HDR 中间目标颜色图像（附件 2），finalLayout=SHADER_READ_ONLY。
    // 注意：resolve 目标格式必须与多重采样附件格式兼容，故 HDR 通道使用 HDR 格式的
    // 多重采样颜色附件（与 LDR 通道的交换链格式不同）。
    {
        VkAttachmentDescription hdrMsaaColor = msaaColor;
        hdrMsaaColor.format = m_hdrFormat;

        VkAttachmentDescription resolveAttachment{};
        resolveAttachment.format = m_hdrFormat;
        resolveAttachment.samples = VK_SAMPLE_COUNT_1_BIT;
        resolveAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        resolveAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        resolveAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        resolveAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        resolveAttachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        resolveAttachment.finalLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

        VkAttachmentReference resolveRef{};
        resolveRef.attachment = 2;
        resolveRef.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

        VkSubpassDescription subpass{};
        subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
        subpass.colorAttachmentCount = 1;
        subpass.pColorAttachments = &msaaColorRef;
        subpass.pResolveAttachments = &resolveRef;
        subpass.pDepthStencilAttachment = &msaaDepthRef;

        VkSubpassDependency dependencies[2]{};
        dependencies[0].srcSubpass = VK_SUBPASS_EXTERNAL;
        dependencies[0].dstSubpass = 0;
        dependencies[0].srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT |
                                       VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
        dependencies[0].srcAccessMask = 0;
        dependencies[0].dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT |
                                        VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
        dependencies[0].dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT |
                                         VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
        dependencies[1].srcSubpass = 0;
        dependencies[1].dstSubpass = VK_SUBPASS_EXTERNAL;
        dependencies[1].srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        dependencies[1].srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
        dependencies[1].dstStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
        dependencies[1].dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

        std::vector<VkAttachmentDescription> attachments = { hdrMsaaColor, msaaDepth, resolveAttachment };
        VkRenderPassCreateInfo info{};
        info.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
        info.attachmentCount = static_cast<uint32_t>(attachments.size());
        info.pAttachments = attachments.data();
        info.subpassCount = 1;
        info.pSubpasses = &subpass;
        info.dependencyCount = 2;
        info.pDependencies = dependencies;
        if (vkCreateRenderPass(m_device, &info, nullptr, &m_msaaHdrRenderPass) != VK_SUCCESS) {
            return Fail("3D: 创建 MSAA HDR RenderPass 失败");
        }
    }

    return true;
}

// 创建多重采样颜色/深度图像、视图与帧缓冲。
bool VKRender3D::CreateMsaaColorDepthResources() {
    if (m_hdrFormat == VK_FORMAT_UNDEFINED) return false;
    if (m_depthImageView == VK_NULL_HANDLE) return false;

    // 多重采样颜色图像。
    VkImageCreateInfo colorInfo{};
    colorInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    colorInfo.imageType = VK_IMAGE_TYPE_2D;
    colorInfo.extent = { m_swapchainExtent.width, m_swapchainExtent.height, 1 };
    colorInfo.mipLevels = 1;
    colorInfo.arrayLayers = 1;
    colorInfo.format = m_swapchainImageFormat;
    colorInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
    colorInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    colorInfo.usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
    colorInfo.samples = m_msaaSampleCount;
    colorInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    if (vkCreateImage(m_device, &colorInfo, nullptr, &m_msaaColorImage) != VK_SUCCESS) {
        DestroyMsaaResources();
        return Fail("3D: 创建 MSAA 颜色图像失败");
    }
    VkMemoryRequirements colorReqs{};
    vkGetImageMemoryRequirements(m_device, m_msaaColorImage, &colorReqs);
    VkMemoryAllocateInfo colorAlloc{};
    colorAlloc.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    colorAlloc.allocationSize = colorReqs.size;
    colorAlloc.memoryTypeIndex = FindMemoryType(colorReqs.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    if (vkAllocateMemory(m_device, &colorAlloc, nullptr, &m_msaaColorMemory) != VK_SUCCESS) {
        DestroyMsaaResources();
        return Fail("3D: 分配 MSAA 颜色内存失败");
    }
    vkBindImageMemory(m_device, m_msaaColorImage, m_msaaColorMemory, 0);

    VkImageViewCreateInfo colorViewInfo{};
    colorViewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    colorViewInfo.image = m_msaaColorImage;
    colorViewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
    colorViewInfo.format = m_swapchainImageFormat;
    colorViewInfo.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
    if (vkCreateImageView(m_device, &colorViewInfo, nullptr, &m_msaaColorView) != VK_SUCCESS) {
        DestroyMsaaResources();
        return Fail("3D: 创建 MSAA 颜色视图失败");
    }

    // HDR 多重采样颜色图像（格式 = HDR 格式，供 HDR 通道 resolve）。
    VkImageCreateInfo hdrColorInfo = colorInfo;
    hdrColorInfo.format = m_hdrFormat;
    if (vkCreateImage(m_device, &hdrColorInfo, nullptr, &m_msaaHdrColorImage) != VK_SUCCESS) {
        DestroyMsaaResources();
        return Fail("3D: 创建 MSAA HDR 颜色图像失败");
    }
    VkMemoryRequirements hdrColorReqs{};
    vkGetImageMemoryRequirements(m_device, m_msaaHdrColorImage, &hdrColorReqs);
    VkMemoryAllocateInfo hdrColorAlloc{};
    hdrColorAlloc.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    hdrColorAlloc.allocationSize = hdrColorReqs.size;
    hdrColorAlloc.memoryTypeIndex = FindMemoryType(hdrColorReqs.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    if (vkAllocateMemory(m_device, &hdrColorAlloc, nullptr, &m_msaaHdrColorMemory) != VK_SUCCESS) {
        DestroyMsaaResources();
        return Fail("3D: 分配 MSAA HDR 颜色内存失败");
    }
    vkBindImageMemory(m_device, m_msaaHdrColorImage, m_msaaHdrColorMemory, 0);

    VkImageViewCreateInfo hdrColorViewInfo = colorViewInfo;
    hdrColorViewInfo.image = m_msaaHdrColorImage;
    hdrColorViewInfo.format = m_hdrFormat;
    if (vkCreateImageView(m_device, &hdrColorViewInfo, nullptr, &m_msaaHdrColorView) != VK_SUCCESS) {
        DestroyMsaaResources();
        return Fail("3D: 创建 MSAA HDR 颜色视图失败");
    }

    // 多重采样深度图像（供深度解析采样，故需 SAMPLED_BIT）。
    VkFormat msaaDepthFormat = FindSupportedFormat(
        { VK_FORMAT_D32_SFLOAT, VK_FORMAT_D32_SFLOAT_S8_UINT, VK_FORMAT_D24_UNORM_S8_UINT },
        VK_IMAGE_TILING_OPTIMAL,
        VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT);
    VkImageCreateInfo depthInfo{};
    depthInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    depthInfo.imageType = VK_IMAGE_TYPE_2D;
    depthInfo.extent = { m_swapchainExtent.width, m_swapchainExtent.height, 1 };
    depthInfo.mipLevels = 1;
    depthInfo.arrayLayers = 1;
    depthInfo.format = msaaDepthFormat;
    depthInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
    depthInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    depthInfo.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    depthInfo.samples = m_msaaSampleCount;
    depthInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    if (vkCreateImage(m_device, &depthInfo, nullptr, &m_msaaDepthImage) != VK_SUCCESS) {
        DestroyMsaaResources();
        return Fail("3D: 创建 MSAA 深度图像失败");
    }
    VkMemoryRequirements depthReqs{};
    vkGetImageMemoryRequirements(m_device, m_msaaDepthImage, &depthReqs);
    VkMemoryAllocateInfo depthAlloc{};
    depthAlloc.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    depthAlloc.allocationSize = depthReqs.size;
    depthAlloc.memoryTypeIndex = FindMemoryType(depthReqs.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    if (vkAllocateMemory(m_device, &depthAlloc, nullptr, &m_msaaDepthMemory) != VK_SUCCESS) {
        DestroyMsaaResources();
        return Fail("3D: 分配 MSAA 深度内存失败");
    }
    vkBindImageMemory(m_device, m_msaaDepthImage, m_msaaDepthMemory, 0);

    VkImageViewCreateInfo depthViewInfo{};
    depthViewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    depthViewInfo.image = m_msaaDepthImage;
    depthViewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
    depthViewInfo.format = depthInfo.format;
    depthViewInfo.subresourceRange = { VK_IMAGE_ASPECT_DEPTH_BIT, 0, 1, 0, 1 };
    if (vkCreateImageView(m_device, &depthViewInfo, nullptr, &m_msaaDepthView) != VK_SUCCESS) {
        DestroyMsaaResources();
        return Fail("3D: 创建 MSAA 深度视图失败");
    }

    // LDR 帧缓冲（每交换链图像一个）：MS 颜色 + MS 深度 + 交换链图像（resolve 目标）。
    m_msaaLdrFramebuffers.assign(m_swapchainImageViews.size(), VK_NULL_HANDLE);
    for (size_t i = 0; i < m_swapchainImageViews.size(); ++i) {
        VkImageView attachments[] = { m_msaaColorView, m_msaaDepthView, m_swapchainImageViews[i] };
        VkFramebufferCreateInfo fbInfo{};
        fbInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
        fbInfo.renderPass = m_msaaLdrRenderPass;
        fbInfo.attachmentCount = 3;
        fbInfo.pAttachments = attachments;
        fbInfo.width = m_swapchainExtent.width;
        fbInfo.height = m_swapchainExtent.height;
        fbInfo.layers = 1;
        if (vkCreateFramebuffer(m_device, &fbInfo, nullptr, &m_msaaLdrFramebuffers[i]) != VK_SUCCESS) {
            DestroyMsaaResources();
            return Fail("3D: 创建 MSAA LDR 帧缓冲失败");
        }
    }

    // HDR 帧缓冲（单个）：MS HDR 颜色 + MS 深度 + HDR 颜色图像（resolve 目标）。
    {
        VkImageView attachments[] = { m_msaaHdrColorView, m_msaaDepthView, m_hdrColorView };
        VkFramebufferCreateInfo fbInfo{};
        fbInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
        fbInfo.renderPass = m_msaaHdrRenderPass;
        fbInfo.attachmentCount = 3;
        fbInfo.pAttachments = attachments;
        fbInfo.width = m_swapchainExtent.width;
        fbInfo.height = m_swapchainExtent.height;
        fbInfo.layers = 1;
        if (vkCreateFramebuffer(m_device, &fbInfo, nullptr, &m_msaaHdrFramebuffer) != VK_SUCCESS) {
            DestroyMsaaResources();
            return Fail("3D: 创建 MSAA HDR 帧缓冲失败");
        }
    }

    // 深度解析帧缓冲（单采样深度视图）。
    {
        VkImageView attachment = m_depthImageView;
        VkFramebufferCreateInfo fbInfo{};
        fbInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
        fbInfo.renderPass = m_depthResolveRenderPass;
        fbInfo.attachmentCount = 1;
        fbInfo.pAttachments = &attachment;
        fbInfo.width = m_swapchainExtent.width;
        fbInfo.height = m_swapchainExtent.height;
        fbInfo.layers = 1;
        if (vkCreateFramebuffer(m_device, &fbInfo, nullptr, &m_depthResolveFramebuffer) != VK_SUCCESS) {
            DestroyMsaaResources();
            return Fail("3D: 创建深度解析帧缓冲失败");
        }
    }

    return true;
}

// 创建多重采样场景管线（LDR 与 HDR 两套，共用 m_pipelineLayout）。
bool VKRender3D::CreateMsaaPipelines() {
    if (m_msaaLdrPipelines[DT_TRIANGLE] != VK_NULL_HANDLE) return true;

    const std::string vertPath = AssetPath::ShaderFile("3d_vert.spv");
    const std::string fragPath = AssetPath::ShaderFile("3d_frag.spv");
    VkShaderModule vertModule = CreateShaderModuleHelper(ReadShaderFile(vertPath), vertPath);
    VkShaderModule fragModule = CreateShaderModuleHelper(ReadShaderFile(fragPath), fragPath);

    VkPipelineShaderStageCreateInfo stages[2]{};
    stages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
    stages[0].module = vertModule;
    stages[0].pName = "main";
    stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    stages[1].module = fragModule;
    stages[1].pName = "main";

    auto bindingDescription = Vertex3D::GetBindingDescription();
    auto attributeDescriptions = Vertex3D::GetAttributeDescriptions();
    VkPipelineVertexInputStateCreateInfo vertexInputInfo{};
    vertexInputInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    vertexInputInfo.vertexBindingDescriptionCount = 1;
    vertexInputInfo.pVertexBindingDescriptions = &bindingDescription;
    vertexInputInfo.vertexAttributeDescriptionCount = static_cast<uint32_t>(attributeDescriptions.size());
    vertexInputInfo.pVertexAttributeDescriptions = attributeDescriptions.data();

    std::vector<VkDynamicState> dynamicStates = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };
    VkPipelineDynamicStateCreateInfo dynamicState{};
    dynamicState.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    dynamicState.dynamicStateCount = static_cast<uint32_t>(dynamicStates.size());
    dynamicState.pDynamicStates = dynamicStates.data();

    DrawTopology topologies[] = { DT_TRIANGLE, DT_TRIANGLE_WIREFRAME, DT_LINE, DT_POINT, DT_TRIANGLE_BLEND };
    for (auto topo : topologies) {
        VkPipelineInputAssemblyStateCreateInfo inputAssembly{};
        inputAssembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
        inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

        VkPipelineRasterizationStateCreateInfo rasterizer{};
        rasterizer.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
        rasterizer.polygonMode = VK_POLYGON_MODE_FILL;
        rasterizer.lineWidth = 1.0f;
        rasterizer.cullMode = VK_CULL_MODE_NONE;
        rasterizer.frontFace = VK_FRONT_FACE_CLOCKWISE;

        if (topo == DT_TRIANGLE_WIREFRAME) {
            rasterizer.polygonMode = VK_POLYGON_MODE_LINE;
        } else if (topo == DT_LINE) {
            inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_LINE_LIST;
        } else if (topo == DT_POINT) {
            inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_POINT_LIST;
        }

        VkPipelineViewportStateCreateInfo viewportState{};
        viewportState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
        viewportState.viewportCount = 1;
        viewportState.scissorCount = 1;

        VkPipelineMultisampleStateCreateInfo multisampling{};
        multisampling.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
        multisampling.rasterizationSamples = m_msaaSampleCount;

        VkPipelineDepthStencilStateCreateInfo depthStencil{};
        depthStencil.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
        depthStencil.depthTestEnable = VK_TRUE;
        depthStencil.depthWriteEnable = VK_TRUE;
        depthStencil.depthCompareOp = VK_COMPARE_OP_LESS;

        VkPipelineColorBlendAttachmentState colorBlendAttachment{};
        colorBlendAttachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                                               VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
        colorBlendAttachment.blendEnable = VK_FALSE;
        if (topo == DT_TRIANGLE_BLEND) {
            depthStencil.depthWriteEnable = VK_FALSE;
            colorBlendAttachment.blendEnable = VK_TRUE;
            colorBlendAttachment.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
            colorBlendAttachment.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
            colorBlendAttachment.colorBlendOp = VK_BLEND_OP_ADD;
            colorBlendAttachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
            colorBlendAttachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
            colorBlendAttachment.alphaBlendOp = VK_BLEND_OP_ADD;
        }

        VkPipelineColorBlendStateCreateInfo colorBlending{};
        colorBlending.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
        colorBlending.attachmentCount = 1;
        colorBlending.pAttachments = &colorBlendAttachment;

        VkGraphicsPipelineCreateInfo pipelineInfo{};
        pipelineInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
        pipelineInfo.stageCount = 2;
        pipelineInfo.pStages = stages;
        pipelineInfo.pVertexInputState = &vertexInputInfo;
        pipelineInfo.pInputAssemblyState = &inputAssembly;
        pipelineInfo.pViewportState = &viewportState;
        pipelineInfo.pRasterizationState = &rasterizer;
        pipelineInfo.pMultisampleState = &multisampling;
        pipelineInfo.pDepthStencilState = &depthStencil;
        pipelineInfo.pColorBlendState = &colorBlending;
        pipelineInfo.pDynamicState = &dynamicState;
        pipelineInfo.layout = m_pipelineLayout;
        pipelineInfo.subpass = 0;

        pipelineInfo.renderPass = m_msaaLdrRenderPass;
        if (vkCreateGraphicsPipelines(m_device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr,
                &m_msaaLdrPipelines[topo]) != VK_SUCCESS) {
            vkDestroyShaderModule(m_device, fragModule, nullptr);
            vkDestroyShaderModule(m_device, vertModule, nullptr);
            return Fail("3D: 创建 MSAA LDR 管线失败");
        }
        pipelineInfo.renderPass = m_msaaHdrRenderPass;
        if (vkCreateGraphicsPipelines(m_device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr,
                &m_msaaHdrPipelines[topo]) != VK_SUCCESS) {
            vkDestroyShaderModule(m_device, fragModule, nullptr);
            vkDestroyShaderModule(m_device, vertModule, nullptr);
            return Fail("3D: 创建 MSAA HDR 管线失败");
        }
    }

    vkDestroyShaderModule(m_device, fragModule, nullptr);
    vkDestroyShaderModule(m_device, vertModule, nullptr);
    return true;
}

// 创建深度解析通道（多重采样深度 -> 单采样深度，供世界坐标回读）。
bool VKRender3D::CreateDepthResolveSupport() {
    if (m_depthResolveRenderPass != VK_NULL_HANDLE) return true;

    VkAttachmentDescription depthAttachment{};
    depthAttachment.format = FindDepthFormat();
    depthAttachment.samples = VK_SAMPLE_COUNT_1_BIT;
    depthAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    depthAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    depthAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    depthAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    depthAttachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    depthAttachment.finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

    VkAttachmentReference depthRef{};
    depthRef.attachment = 0;
    depthRef.layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

    VkSubpassDescription subpass{};
    subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.pDepthStencilAttachment = &depthRef;

    VkSubpassDependency dependency{};
    dependency.srcSubpass = VK_SUBPASS_EXTERNAL;
    dependency.dstSubpass = 0;
    dependency.srcStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
    dependency.srcAccessMask = VK_ACCESS_SHADER_READ_BIT;
    dependency.dstStageMask = VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
    dependency.dstAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;

    VkRenderPassCreateInfo info{};
    info.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    info.attachmentCount = 1;
    info.pAttachments = &depthAttachment;
    info.subpassCount = 1;
    info.pSubpasses = &subpass;
    info.dependencyCount = 1;
    info.pDependencies = &dependency;
    if (vkCreateRenderPass(m_device, &info, nullptr, &m_depthResolveRenderPass) != VK_SUCCESS) {
        return Fail("3D: 创建深度解析 RenderPass 失败");
    }

    // 采样器（多重采样深度）。
    VkSamplerCreateInfo samplerInfo{};
    samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    samplerInfo.magFilter = VK_FILTER_NEAREST;
    samplerInfo.minFilter = VK_FILTER_NEAREST;
    samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
    samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.maxAnisotropy = 1.0f;
    if (vkCreateSampler(m_device, &samplerInfo, nullptr, &m_msaaDepthSampler) != VK_SUCCESS) {
        return Fail("3D: 创建 MSAA 深度采样器失败");
    }

    // 描述符集布局 + 池 + 集。
    VkDescriptorSetLayoutBinding binding{};
    binding.binding = 0;
    binding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    binding.descriptorCount = 1;
    binding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    VkDescriptorSetLayoutCreateInfo setInfo{};
    setInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    setInfo.bindingCount = 1;
    setInfo.pBindings = &binding;
    if (vkCreateDescriptorSetLayout(m_device, &setInfo, nullptr, &m_depthResolveSetLayout) != VK_SUCCESS) {
        return Fail("3D: 创建深度解析描述符布局失败");
    }

    VkDescriptorPoolSize poolSize{};
    poolSize.type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    poolSize.descriptorCount = 1;
    VkDescriptorPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    poolInfo.poolSizeCount = 1;
    poolInfo.pPoolSizes = &poolSize;
    poolInfo.maxSets = 1;
    if (vkCreateDescriptorPool(m_device, &poolInfo, nullptr, &m_depthResolvePool) != VK_SUCCESS) {
        return Fail("3D: 创建深度解析描述符池失败");
    }

    VkDescriptorSetAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    allocInfo.descriptorPool = m_depthResolvePool;
    allocInfo.descriptorSetCount = 1;
    allocInfo.pSetLayouts = &m_depthResolveSetLayout;
    if (vkAllocateDescriptorSets(m_device, &allocInfo, &m_depthResolveSet) != VK_SUCCESS) {
        return Fail("3D: 分配深度解析描述符集失败");
    }

    // 管线。
    VkPipelineLayoutCreateInfo layoutInfo{};
    layoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    layoutInfo.setLayoutCount = 1;
    layoutInfo.pSetLayouts = &m_depthResolveSetLayout;
    if (vkCreatePipelineLayout(m_device, &layoutInfo, nullptr, &m_depthResolveLayout) != VK_SUCCESS) {
        return Fail("3D: 创建深度解析管线布局失败");
    }

    const std::string vertPath = AssetPath::ShaderFile("depth_resolve_vert.spv");
    const std::string fragPath = AssetPath::ShaderFile("depth_resolve_frag.spv");
    VkShaderModule vertModule = CreateShaderModuleHelper(ReadShaderFile(vertPath), vertPath);
    VkShaderModule fragModule = CreateShaderModuleHelper(ReadShaderFile(fragPath), fragPath);

    VkPipelineShaderStageCreateInfo stages[2]{};
    stages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
    stages[0].module = vertModule;
    stages[0].pName = "main";
    stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    stages[1].module = fragModule;
    stages[1].pName = "main";

    VkPipelineVertexInputStateCreateInfo vertexInput{};
    vertexInput.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;

    VkPipelineInputAssemblyStateCreateInfo inputAssembly{};
    inputAssembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

    std::vector<VkDynamicState> dynamicStates = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };
    VkPipelineDynamicStateCreateInfo dynamicState{};
    dynamicState.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    dynamicState.dynamicStateCount = static_cast<uint32_t>(dynamicStates.size());
    dynamicState.pDynamicStates = dynamicStates.data();

    VkPipelineViewportStateCreateInfo viewportState{};
    viewportState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    viewportState.viewportCount = 1;
    viewportState.scissorCount = 1;

    VkPipelineRasterizationStateCreateInfo rasterizer{};
    rasterizer.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    rasterizer.polygonMode = VK_POLYGON_MODE_FILL;
    rasterizer.lineWidth = 1.0f;
    rasterizer.cullMode = VK_CULL_MODE_NONE;
    rasterizer.frontFace = VK_FRONT_FACE_CLOCKWISE;

    VkPipelineMultisampleStateCreateInfo multisampling{};
    multisampling.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    multisampling.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

    VkPipelineDepthStencilStateCreateInfo depthStencil{};
    depthStencil.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    depthStencil.depthTestEnable = VK_TRUE;
    depthStencil.depthWriteEnable = VK_TRUE;
    depthStencil.depthCompareOp = VK_COMPARE_OP_ALWAYS;  // 用 gl_FragDepth 直接写入

    VkPipelineColorBlendStateCreateInfo colorBlending{};
    colorBlending.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    colorBlending.attachmentCount = 0;

    VkGraphicsPipelineCreateInfo pipelineInfo{};
    pipelineInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    pipelineInfo.stageCount = 2;
    pipelineInfo.pStages = stages;
    pipelineInfo.pVertexInputState = &vertexInput;
    pipelineInfo.pInputAssemblyState = &inputAssembly;
    pipelineInfo.pViewportState = &viewportState;
    pipelineInfo.pRasterizationState = &rasterizer;
    pipelineInfo.pMultisampleState = &multisampling;
    pipelineInfo.pDepthStencilState = &depthStencil;
    pipelineInfo.pColorBlendState = &colorBlending;
    pipelineInfo.pDynamicState = &dynamicState;
    pipelineInfo.layout = m_depthResolveLayout;
    pipelineInfo.renderPass = m_depthResolveRenderPass;
    pipelineInfo.subpass = 0;

    VkResult result = vkCreateGraphicsPipelines(m_device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr,
        &m_depthResolvePipeline);
    vkDestroyShaderModule(m_device, fragModule, nullptr);
    vkDestroyShaderModule(m_device, vertModule, nullptr);
    if (result != VK_SUCCESS) {
        return Fail("3D: 创建深度解析管线失败");
    }

    return true;
}

// 写入深度解析描述符集（多重采样深度视图 + 采样器）。
void VKRender3D::UpdateDepthResolveDescriptors() {
    if (m_device == VK_NULL_HANDLE || m_depthResolveSet == VK_NULL_HANDLE) return;
    if (m_msaaDepthSampler == VK_NULL_HANDLE || m_msaaDepthView == VK_NULL_HANDLE) return;

    VkDescriptorImageInfo imageInfo{};
    imageInfo.sampler = m_msaaDepthSampler;
    imageInfo.imageView = m_msaaDepthView;
    imageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

    VkWriteDescriptorSet write{};
    write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    write.dstSet = m_depthResolveSet;
    write.dstBinding = 0;
    write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    write.descriptorCount = 1;
    write.pImageInfo = &imageInfo;
    vkUpdateDescriptorSets(m_device, 1, &write, 0, nullptr);
}

// 在命令缓冲中记录深度解析通道（把多重采样深度解析为单采样 m_depthImage）。
void VKRender3D::RecordDepthResolve() {
    if (m_depthResolveRenderPass == VK_NULL_HANDLE || m_depthResolveFramebuffer == VK_NULL_HANDLE ||
        m_depthResolvePipeline == VK_NULL_HANDLE || m_msaaDepthView == VK_NULL_HANDLE) {
        return;
    }
    VkCommandBuffer cmd = m_commandBuffers[m_currentFrame];

    // 多重采样深度：着色器读取 -> 布局转换到 SHADER_READ_ONLY。
    VkImageMemoryBarrier toRead{};
    toRead.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    toRead.oldLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
    toRead.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    toRead.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    toRead.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    toRead.image = m_msaaDepthImage;
    toRead.subresourceRange = { VK_IMAGE_ASPECT_DEPTH_BIT, 0, 1, 0, 1 };
    toRead.srcAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
    toRead.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    vkCmdPipelineBarrier(cmd,
        VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
        0, 0, nullptr, 0, nullptr, 1, &toRead);

    VkRenderPassBeginInfo renderPassInfo{};
    renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    renderPassInfo.renderPass = m_depthResolveRenderPass;
    renderPassInfo.framebuffer = m_depthResolveFramebuffer;
    renderPassInfo.renderArea.extent = m_swapchainExtent;
    VkClearValue clearValue{};
    clearValue.depthStencil = { 1.0f, 0 };
    renderPassInfo.clearValueCount = 1;
    renderPassInfo.pClearValues = &clearValue;
    vkCmdBeginRenderPass(cmd, &renderPassInfo, VK_SUBPASS_CONTENTS_INLINE);

    VkViewport viewport{};
    viewport.width = static_cast<float>(m_swapchainExtent.width);
    viewport.height = static_cast<float>(m_swapchainExtent.height);
    viewport.minDepth = 0.0f;
    viewport.maxDepth = 1.0f;
    vkCmdSetViewport(cmd, 0, 1, &viewport);
    VkRect2D scissor{};
    scissor.extent = m_swapchainExtent;
    vkCmdSetScissor(cmd, 0, 1, &scissor);

    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_depthResolvePipeline);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_depthResolveLayout,
                            0, 1, &m_depthResolveSet, 0, nullptr);
    vkCmdDraw(cmd, 3, 1, 0, 0);
    vkCmdEndRenderPass(cmd);

    // 还原多重采样深度布局，供下一帧继续作为深度附件使用。
    VkImageMemoryBarrier toAttachment = toRead;
    toAttachment.oldLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    toAttachment.newLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
    toAttachment.srcAccessMask = VK_ACCESS_SHADER_READ_BIT;
    toAttachment.dstAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
    vkCmdPipelineBarrier(cmd,
        VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT,
        0, 0, nullptr, 0, nullptr, 1, &toAttachment);
}

// 销毁尺寸相关的 MSAA 资源（图像/视图/帧缓冲/解析帧缓冲）。
void VKRender3D::DestroyMsaaResources() {
    if (m_device == VK_NULL_HANDLE) {
        m_msaaLdrFramebuffers.clear();
        m_msaaHdrFramebuffer = VK_NULL_HANDLE;
        m_depthResolveFramebuffer = VK_NULL_HANDLE;
        m_msaaColorView = VK_NULL_HANDLE;
        m_msaaColorMemory = VK_NULL_HANDLE;
        m_msaaColorImage = VK_NULL_HANDLE;
        m_msaaHdrColorView = VK_NULL_HANDLE;
        m_msaaHdrColorMemory = VK_NULL_HANDLE;
        m_msaaHdrColorImage = VK_NULL_HANDLE;
        m_msaaDepthView = VK_NULL_HANDLE;
        m_msaaDepthMemory = VK_NULL_HANDLE;
        m_msaaDepthImage = VK_NULL_HANDLE;
        m_msaaTargetsReady = false;
        return;
    }
    for (VkFramebuffer fb : m_msaaLdrFramebuffers) {
        if (fb != VK_NULL_HANDLE) vkDestroyFramebuffer(m_device, fb, nullptr);
    }
    m_msaaLdrFramebuffers.clear();
    if (m_msaaHdrFramebuffer != VK_NULL_HANDLE) {
        vkDestroyFramebuffer(m_device, m_msaaHdrFramebuffer, nullptr);
        m_msaaHdrFramebuffer = VK_NULL_HANDLE;
    }
    if (m_depthResolveFramebuffer != VK_NULL_HANDLE) {
        vkDestroyFramebuffer(m_device, m_depthResolveFramebuffer, nullptr);
        m_depthResolveFramebuffer = VK_NULL_HANDLE;
    }
    if (m_msaaColorView != VK_NULL_HANDLE) {
        vkDestroyImageView(m_device, m_msaaColorView, nullptr);
        m_msaaColorView = VK_NULL_HANDLE;
    }
    if (m_msaaColorMemory != VK_NULL_HANDLE) {
        vkFreeMemory(m_device, m_msaaColorMemory, nullptr);
        m_msaaColorMemory = VK_NULL_HANDLE;
    }
    if (m_msaaColorImage != VK_NULL_HANDLE) {
        vkDestroyImage(m_device, m_msaaColorImage, nullptr);
        m_msaaColorImage = VK_NULL_HANDLE;
    }
    if (m_msaaHdrColorView != VK_NULL_HANDLE) {
        vkDestroyImageView(m_device, m_msaaHdrColorView, nullptr);
        m_msaaHdrColorView = VK_NULL_HANDLE;
    }
    if (m_msaaHdrColorMemory != VK_NULL_HANDLE) {
        vkFreeMemory(m_device, m_msaaHdrColorMemory, nullptr);
        m_msaaHdrColorMemory = VK_NULL_HANDLE;
    }
    if (m_msaaHdrColorImage != VK_NULL_HANDLE) {
        vkDestroyImage(m_device, m_msaaHdrColorImage, nullptr);
        m_msaaHdrColorImage = VK_NULL_HANDLE;
    }
    if (m_msaaDepthView != VK_NULL_HANDLE) {
        vkDestroyImageView(m_device, m_msaaDepthView, nullptr);
        m_msaaDepthView = VK_NULL_HANDLE;
    }
    if (m_msaaDepthMemory != VK_NULL_HANDLE) {
        vkFreeMemory(m_device, m_msaaDepthMemory, nullptr);
        m_msaaDepthMemory = VK_NULL_HANDLE;
    }
    if (m_msaaDepthImage != VK_NULL_HANDLE) {
        vkDestroyImage(m_device, m_msaaDepthImage, nullptr);
        m_msaaDepthImage = VK_NULL_HANDLE;
    }
    m_msaaTargetsReady = false;
}

// 销毁非尺寸相关的 MSAA 支持资源（渲染通道/管线/描述符/采样器）。
void VKRender3D::DestroyMsaaSupport() {
    if (m_device != VK_NULL_HANDLE) {
        for (int i = 0; i < DT_COUNT; ++i) {
            if (m_msaaLdrPipelines[i] != VK_NULL_HANDLE) {
                vkDestroyPipeline(m_device, m_msaaLdrPipelines[i], nullptr);
                m_msaaLdrPipelines[i] = VK_NULL_HANDLE;
            }
            if (m_msaaHdrPipelines[i] != VK_NULL_HANDLE) {
                vkDestroyPipeline(m_device, m_msaaHdrPipelines[i], nullptr);
                m_msaaHdrPipelines[i] = VK_NULL_HANDLE;
            }
        }
        if (m_depthResolvePipeline != VK_NULL_HANDLE) {
            vkDestroyPipeline(m_device, m_depthResolvePipeline, nullptr);
            m_depthResolvePipeline = VK_NULL_HANDLE;
        }
        if (m_depthResolveLayout != VK_NULL_HANDLE) {
            vkDestroyPipelineLayout(m_device, m_depthResolveLayout, nullptr);
            m_depthResolveLayout = VK_NULL_HANDLE;
        }
        if (m_depthResolvePool != VK_NULL_HANDLE) {
            vkDestroyDescriptorPool(m_device, m_depthResolvePool, nullptr);
            m_depthResolvePool = VK_NULL_HANDLE;
        }
        if (m_depthResolveSetLayout != VK_NULL_HANDLE) {
            vkDestroyDescriptorSetLayout(m_device, m_depthResolveSetLayout, nullptr);
            m_depthResolveSetLayout = VK_NULL_HANDLE;
        }
        if (m_msaaDepthSampler != VK_NULL_HANDLE) {
            vkDestroySampler(m_device, m_msaaDepthSampler, nullptr);
            m_msaaDepthSampler = VK_NULL_HANDLE;
        }
        if (m_depthResolveRenderPass != VK_NULL_HANDLE) {
            vkDestroyRenderPass(m_device, m_depthResolveRenderPass, nullptr);
            m_depthResolveRenderPass = VK_NULL_HANDLE;
        }
        if (m_msaaLdrRenderPass != VK_NULL_HANDLE) {
            vkDestroyRenderPass(m_device, m_msaaLdrRenderPass, nullptr);
            m_msaaLdrRenderPass = VK_NULL_HANDLE;
        }
        if (m_msaaHdrRenderPass != VK_NULL_HANDLE) {
            vkDestroyRenderPass(m_device, m_msaaHdrRenderPass, nullptr);
            m_msaaHdrRenderPass = VK_NULL_HANDLE;
        }
    }
    m_depthResolveSet = VK_NULL_HANDLE;
    m_msaaSupportReady = false;
}


// 创建渲染通道。
bool VKRender3D::CreateRenderPass() {
    VkAttachmentDescription colorAttachment{};
    colorAttachment.format = m_swapchainImageFormat;
    colorAttachment.samples = VK_SAMPLE_COUNT_1_BIT;
    colorAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    colorAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    colorAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    colorAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    colorAttachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    colorAttachment.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

    VkAttachmentReference colorAttachmentRef{};
    colorAttachmentRef.attachment = 0;
    colorAttachmentRef.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

    VkAttachmentDescription depthAttachment{};
    depthAttachment.format = FindDepthFormat();
    depthAttachment.samples = VK_SAMPLE_COUNT_1_BIT;
    depthAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    depthAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    depthAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    depthAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    depthAttachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    depthAttachment.finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

    VkAttachmentReference depthAttachmentRef{};
    depthAttachmentRef.attachment = 1;
    depthAttachmentRef.layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

    VkSubpassDescription subpass{};
    subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.colorAttachmentCount = 1;
    subpass.pColorAttachments = &colorAttachmentRef;
    subpass.pDepthStencilAttachment = &depthAttachmentRef;

    VkSubpassDependency dependency{};
    dependency.srcSubpass = VK_SUBPASS_EXTERNAL;
    dependency.dstSubpass = 0;
    dependency.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT |
                              VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
    dependency.srcAccessMask = 0;
    dependency.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT |
                               VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
    dependency.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT |
                                VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;

    std::vector<VkAttachmentDescription> attachments = { colorAttachment, depthAttachment };

    VkRenderPassCreateInfo renderPassInfo{};
    renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    renderPassInfo.attachmentCount = static_cast<uint32_t>(attachments.size());
    renderPassInfo.pAttachments = attachments.data();
    renderPassInfo.subpassCount = 1;
    renderPassInfo.pSubpasses = &subpass;
    renderPassInfo.dependencyCount = 1;
    renderPassInfo.pDependencies = &dependency;

    if (vkCreateRenderPass(m_device, &renderPassInfo, nullptr, &m_renderPass) != VK_SUCCESS) {
        return Fail("3D: 创建 RenderPass 失败");
    }

    return true;
}


// 创建图形管线。
bool VKRender3D::CreatePipelines() {
    const std::string vertShaderPath = AssetPath::ShaderFile("3d_vert.spv");
    const std::string fragShaderPath = AssetPath::ShaderFile("3d_frag.spv");
    auto vertShaderCode = ReadShaderFile(vertShaderPath);
    auto fragShaderCode = ReadShaderFile(fragShaderPath);

    VkShaderModule vertShaderModule = CreateShaderModuleHelper(
        vertShaderCode, vertShaderPath);
    VkShaderModule fragShaderModule = CreateShaderModuleHelper(
        fragShaderCode, fragShaderPath);

    VkPipelineShaderStageCreateInfo vertShaderStageInfo{};
    vertShaderStageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    vertShaderStageInfo.stage = VK_SHADER_STAGE_VERTEX_BIT;
    vertShaderStageInfo.module = vertShaderModule;
    vertShaderStageInfo.pName = "main";

    VkPipelineShaderStageCreateInfo fragShaderStageInfo{};
    fragShaderStageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    fragShaderStageInfo.stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    fragShaderStageInfo.module = fragShaderModule;
    fragShaderStageInfo.pName = "main";

    VkPipelineShaderStageCreateInfo shaderStages[] = { vertShaderStageInfo, fragShaderStageInfo };

    auto bindingDescription = Vertex3D::GetBindingDescription();
    auto attributeDescriptions = Vertex3D::GetAttributeDescriptions();

    VkPipelineVertexInputStateCreateInfo vertexInputInfo{};
    vertexInputInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    vertexInputInfo.vertexBindingDescriptionCount = 1;
    vertexInputInfo.pVertexBindingDescriptions = &bindingDescription;
    vertexInputInfo.vertexAttributeDescriptionCount = static_cast<uint32_t>(attributeDescriptions.size());
    vertexInputInfo.pVertexAttributeDescriptions = attributeDescriptions.data();

    {
        VkPushConstantRange pushRange{};
        pushRange.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
        pushRange.offset = 0;
        pushRange.size = sizeof(MaterialParams);

        VkDescriptorSetLayout setLayouts[2] = { m_descriptorSetLayout, m_materialSetLayout };
        VkPipelineLayoutCreateInfo pipelineLayoutInfo{};
        pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
        pipelineLayoutInfo.setLayoutCount = 2;
        pipelineLayoutInfo.pSetLayouts = setLayouts;
        pipelineLayoutInfo.pushConstantRangeCount = 1;
        pipelineLayoutInfo.pPushConstantRanges = &pushRange;
        if (vkCreatePipelineLayout(m_device, &pipelineLayoutInfo, nullptr, &m_pipelineLayout) != VK_SUCCESS) {
            return Fail("3D: 创建管线布局失败");
        }
    }

    std::vector<VkDynamicState> dynamicStates = {
        VK_DYNAMIC_STATE_VIEWPORT,
        VK_DYNAMIC_STATE_SCISSOR
    };
    VkPipelineDynamicStateCreateInfo dynamicState{};
    dynamicState.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    dynamicState.dynamicStateCount = static_cast<uint32_t>(dynamicStates.size());
    dynamicState.pDynamicStates = dynamicStates.data();

    DrawTopology topologies[] = { DT_TRIANGLE, DT_TRIANGLE_WIREFRAME, DT_LINE, DT_POINT, DT_TRIANGLE_BLEND };
    // 1.6：HDR 场景管线渲染到 HDR 中间目标，需先备好 HDR 渲染通道（复用同一管线布局）。
    // HDR 为可选功能：若设备不支持所需 HDR 格式，跳过 HDR 管线（保留 LDR 路径，不使初始化失败）。
    bool hdrPipelinesReady = false;
    try {
        hdrPipelinesReady = CreateHdrRenderPass();
    } catch (...) {
        hdrPipelinesReady = false;
    }
    for (auto topo : topologies) {
        VkPipelineInputAssemblyStateCreateInfo inputAssembly{};
        inputAssembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
        inputAssembly.primitiveRestartEnable = VK_FALSE;

        VkPipelineRasterizationStateCreateInfo rasterizer{};
        rasterizer.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
        rasterizer.depthClampEnable = VK_FALSE;
        rasterizer.rasterizerDiscardEnable = VK_FALSE;
        rasterizer.polygonMode = VK_POLYGON_MODE_FILL;
        rasterizer.lineWidth = 1.0f;
        rasterizer.cullMode = VK_CULL_MODE_NONE;
        rasterizer.frontFace = VK_FRONT_FACE_CLOCKWISE;
        rasterizer.depthBiasEnable = VK_FALSE;

        switch (topo) {
        case DT_TRIANGLE:
            inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
            break;
        case DT_TRIANGLE_WIREFRAME:
            inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
            rasterizer.polygonMode = VK_POLYGON_MODE_LINE;
            rasterizer.cullMode = VK_CULL_MODE_NONE;
            break;
        case DT_LINE:
            inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_LINE_LIST;
            break;
        case DT_POINT:
            inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_POINT_LIST;
            break;
        case DT_TRIANGLE_BLEND:
            inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
            break;
        default:
            inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
            break;
        }

        VkPipelineViewportStateCreateInfo viewportState{};
        viewportState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
        viewportState.viewportCount = 1;
        viewportState.scissorCount = 1;

        VkPipelineMultisampleStateCreateInfo multisampling{};
        multisampling.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
        multisampling.sampleShadingEnable = VK_FALSE;
        multisampling.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

        VkPipelineDepthStencilStateCreateInfo depthStencil{};
        depthStencil.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
        depthStencil.depthTestEnable = VK_TRUE;
        depthStencil.depthWriteEnable = VK_TRUE;
        depthStencil.depthCompareOp = VK_COMPARE_OP_LESS;
        depthStencil.depthBoundsTestEnable = VK_FALSE;
        depthStencil.stencilTestEnable = VK_FALSE;

        VkPipelineColorBlendAttachmentState colorBlendAttachment{};
        colorBlendAttachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                                               VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
        colorBlendAttachment.blendEnable = VK_FALSE;

        // 透明混合：srcAlpha / 1-srcAlpha，且不写深度（按远到近排序绘制）。
        if (topo == DT_TRIANGLE_BLEND) {
            depthStencil.depthWriteEnable = VK_FALSE;
            colorBlendAttachment.blendEnable = VK_TRUE;
            colorBlendAttachment.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
            colorBlendAttachment.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
            colorBlendAttachment.colorBlendOp = VK_BLEND_OP_ADD;
            colorBlendAttachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
            colorBlendAttachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
            colorBlendAttachment.alphaBlendOp = VK_BLEND_OP_ADD;
        }

        VkPipelineColorBlendStateCreateInfo colorBlending{};
        colorBlending.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
        colorBlending.logicOpEnable = VK_FALSE;
        colorBlending.attachmentCount = 1;
        colorBlending.pAttachments = &colorBlendAttachment;

        VkGraphicsPipelineCreateInfo pipelineInfo{};
        pipelineInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
        pipelineInfo.stageCount = 2;
        pipelineInfo.pStages = shaderStages;
        pipelineInfo.pVertexInputState = &vertexInputInfo;
        pipelineInfo.pInputAssemblyState = &inputAssembly;
        pipelineInfo.pViewportState = &viewportState;
        pipelineInfo.pRasterizationState = &rasterizer;
        pipelineInfo.pMultisampleState = &multisampling;
        pipelineInfo.pDepthStencilState = &depthStencil;
        pipelineInfo.pColorBlendState = &colorBlending;
        pipelineInfo.pDynamicState = &dynamicState;
        pipelineInfo.layout = m_pipelineLayout;
        pipelineInfo.renderPass = m_renderPass;
        pipelineInfo.subpass = 0;

        VkResult result = vkCreateGraphicsPipelines(m_device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &m_pipelines[topo]);
        if (result != VK_SUCCESS) {
            vkDestroyShaderModule(m_device, fragShaderModule, nullptr);
            vkDestroyShaderModule(m_device, vertShaderModule, nullptr);
            return Fail("3D: 创建图形管线失败");
        }

        // 1.6：同一状态创建渲染到 HDR 中间目标的对应管线（仅 renderPass 不同）。
        if (hdrPipelinesReady) {
            pipelineInfo.renderPass = m_hdrRenderPass;
            result = vkCreateGraphicsPipelines(m_device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &m_hdrPipelines[topo]);
            if (result != VK_SUCCESS) {
                vkDestroyShaderModule(m_device, fragShaderModule, nullptr);
                vkDestroyShaderModule(m_device, vertShaderModule, nullptr);
                return Fail("3D: 创建 HDR 图形管线失败");
            }
        }
    }

    vkDestroyShaderModule(m_device, fragShaderModule, nullptr);
    vkDestroyShaderModule(m_device, vertShaderModule, nullptr);

    return CreateShadowPipeline();
}


// 创建帧缓冲。
bool VKRender3D::CreateFramebuffers() {
    if (!CreateDepthResources()) return false;

    m_swapchainFramebuffers.resize(m_swapchainImageViews.size());

    for (size_t i = 0; i < m_swapchainImageViews.size(); i++) {
        VkImageView attachments[] = {
            m_swapchainImageViews[i],
            m_depthImageView
        };

        VkFramebufferCreateInfo framebufferInfo{};
        framebufferInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
        framebufferInfo.renderPass = m_renderPass;
        framebufferInfo.attachmentCount = 2;
        framebufferInfo.pAttachments = attachments;
        framebufferInfo.width = m_swapchainExtent.width;
        framebufferInfo.height = m_swapchainExtent.height;
        framebufferInfo.layers = 1;

        if (vkCreateFramebuffer(m_device, &framebufferInfo, nullptr, &m_swapchainFramebuffers[i]) != VK_SUCCESS) {
            return Fail("3D: 创建 Framebuffer 失败");
        }
    }

    return true;
}


// 选择主深度格式。
VkFormat VKRender3D::FindDepthFormat() {
    return FindSupportedFormat(
        { VK_FORMAT_D32_SFLOAT, VK_FORMAT_D32_SFLOAT_S8_UINT, VK_FORMAT_D24_UNORM_S8_UINT },
        VK_IMAGE_TILING_OPTIMAL,
        VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT);
}

// 选择阴影深度格式。
VkFormat VKRender3D::FindShadowDepthFormat() {
    return FindSupportedFormat(
        { VK_FORMAT_D32_SFLOAT, VK_FORMAT_D32_SFLOAT_S8_UINT, VK_FORMAT_D24_UNORM_S8_UINT },
        VK_IMAGE_TILING_OPTIMAL,
        VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT);
}

// 在候选格式里找设备支持项。
VkFormat VKRender3D::FindSupportedFormat(const std::vector<VkFormat>& candidates, VkImageTiling tiling, VkFormatFeatureFlags features) {
    for (VkFormat format : candidates) {
        VkFormatProperties props;
        vkGetPhysicalDeviceFormatProperties(m_physicalDevice, format, &props);

        if (tiling == VK_IMAGE_TILING_LINEAR && (props.linearTilingFeatures & features) == features) {
            return format;
        } else if (tiling == VK_IMAGE_TILING_OPTIMAL && (props.optimalTilingFeatures & features) == features) {
            return format;
        }
    }
    throw std::runtime_error("未找到支持的深度格式");
}

// 查询格式是否含模板。
bool VKRender3D::HasStencilComponent(VkFormat format) {
    return format == VK_FORMAT_D32_SFLOAT_S8_UINT || format == VK_FORMAT_D24_UNORM_S8_UINT;
}

// 创建深度附件。
bool VKRender3D::CreateDepthResources() {
    VkFormat depthFormat = FindDepthFormat();

    VkImageCreateInfo imageInfo{};
    imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    imageInfo.imageType = VK_IMAGE_TYPE_2D;
    imageInfo.extent.width = m_swapchainExtent.width;
    imageInfo.extent.height = m_swapchainExtent.height;
    imageInfo.extent.depth = 1;
    imageInfo.mipLevels = 1;
    imageInfo.arrayLayers = 1;
    imageInfo.format = depthFormat;
    imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
    imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    imageInfo.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
    imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
    imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    if (vkCreateImage(m_device, &imageInfo, nullptr, &m_depthImage) != VK_SUCCESS) {
        std::cerr << "3D: 创建深度图像失败" << std::endl;
        return false;
    }

    VkMemoryRequirements memRequirements;
    vkGetImageMemoryRequirements(m_device, m_depthImage, &memRequirements);

    VkMemoryAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocInfo.allocationSize = memRequirements.size;
    allocInfo.memoryTypeIndex = FindMemoryType(memRequirements.memoryTypeBits,
        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

    if (vkAllocateMemory(m_device, &allocInfo, nullptr, &m_depthImageMemory) != VK_SUCCESS) {
        std::cerr << "3D: 分配深度图像内存失败" << std::endl;
        vkDestroyImage(m_device, m_depthImage, nullptr);
        m_depthImage = VK_NULL_HANDLE;
        return false;
    }

    vkBindImageMemory(m_device, m_depthImage, m_depthImageMemory, 0);

    VkImageViewCreateInfo viewInfo{};
    viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    viewInfo.image = m_depthImage;
    viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
    viewInfo.format = depthFormat;
    viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
    viewInfo.subresourceRange.baseMipLevel = 0;
    viewInfo.subresourceRange.levelCount = 1;
    viewInfo.subresourceRange.baseArrayLayer = 0;
    viewInfo.subresourceRange.layerCount = 1;

    if (vkCreateImageView(m_device, &viewInfo, nullptr, &m_depthImageView) != VK_SUCCESS) {
        std::cerr << "3D: 创建深度图像视图失败" << std::endl;
        DestroyDepthResources();
        return false;
    }

    return true;
}

// 销毁深度附件。
void VKRender3D::DestroyDepthResources() {
    if (m_depthImageView != VK_NULL_HANDLE) {
        vkDestroyImageView(m_device, m_depthImageView, nullptr);
        m_depthImageView = VK_NULL_HANDLE;
    }
    if (m_depthImageMemory != VK_NULL_HANDLE) {
        vkFreeMemory(m_device, m_depthImageMemory, nullptr);
        m_depthImageMemory = VK_NULL_HANDLE;
    }
    if (m_depthImage != VK_NULL_HANDLE) {
        vkDestroyImage(m_device, m_depthImage, nullptr);
        m_depthImage = VK_NULL_HANDLE;
    }
}


// 创建描述符集布局。
bool VKRender3D::CreateDescriptorSetLayout() {
    VkDescriptorSetLayoutBinding uboLayoutBinding{};
    uboLayoutBinding.binding = 0;
    uboLayoutBinding.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    uboLayoutBinding.descriptorCount = 1;
    uboLayoutBinding.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
    uboLayoutBinding.pImmutableSamplers = nullptr;

    VkDescriptorSetLayoutBinding shadowLayoutBinding{};
    shadowLayoutBinding.binding = 1;
    shadowLayoutBinding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    shadowLayoutBinding.descriptorCount = 1;
    shadowLayoutBinding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    shadowLayoutBinding.pImmutableSamplers = nullptr;

    VkDescriptorSetLayoutBinding bindings[] = { uboLayoutBinding, shadowLayoutBinding };

    VkDescriptorSetLayoutCreateInfo layoutInfo{};
    layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    layoutInfo.bindingCount = 2;
    layoutInfo.pBindings = bindings;

    if (vkCreateDescriptorSetLayout(m_device, &layoutInfo, nullptr, &m_descriptorSetLayout) != VK_SUCCESS) {
        std::cerr << "3D: 创建描述符集布局失败" << std::endl;
        return false;
    }
    return true;
}

// 创建材质（set 1）描述符布局与描述符池。
// 1.5：set 1 含 5 个 combined image sampler（baseColor / metallicRoughness /
// normal / occlusion / emissive）。默认白/平面法线纹理在首次需要时惰性创建。
bool VKRender3D::CreateMaterialDescriptors() {
    VkDescriptorSetLayoutBinding textureBindings[kMaterialTextureSlotCount]{};
    for (uint32_t slot = 0; slot < kMaterialTextureSlotCount; ++slot) {
        textureBindings[slot].binding = slot;
        textureBindings[slot].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        textureBindings[slot].descriptorCount = 1;
        textureBindings[slot].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
        textureBindings[slot].pImmutableSamplers = nullptr;
    }

    VkDescriptorSetLayoutCreateInfo layoutInfo{};
    layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    layoutInfo.bindingCount = kMaterialTextureSlotCount;
    layoutInfo.pBindings = textureBindings;
    if (vkCreateDescriptorSetLayout(m_device, &layoutInfo, nullptr, &m_materialSetLayout) != VK_SUCCESS) {
        std::cerr << "3D: 创建材质描述符集布局失败" << std::endl;
        return false;
    }

    VkDescriptorPoolSize poolSize{};
    poolSize.type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    poolSize.descriptorCount = static_cast<uint32_t>(kMaterialDescriptorPoolSize * kMaterialTextureSlotCount);

    VkDescriptorPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    poolInfo.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
    poolInfo.poolSizeCount = 1;
    poolInfo.pPoolSizes = &poolSize;
    poolInfo.maxSets = static_cast<uint32_t>(kMaterialDescriptorPoolSize);
    if (vkCreateDescriptorPool(m_device, &poolInfo, nullptr, &m_materialDescriptorPool) != VK_SUCCESS) {
        std::cerr << "3D: 创建材质描述符池失败" << std::endl;
        return false;
    }
    return true;
}

// 销毁材质描述符资源（含默认白/平面法线纹理句柄与缓存映射）。
void VKRender3D::DestroyMaterialDescriptors() {
    m_materialDescriptorSets.clear();
    m_defaultWhiteHandle = 0;
    m_defaultFlatNormalHandle = 0;
    if (m_materialDescriptorPool != VK_NULL_HANDLE) {
        vkDestroyDescriptorPool(m_device, m_materialDescriptorPool, nullptr);
        m_materialDescriptorPool = VK_NULL_HANDLE;
    }
    if (m_materialSetLayout != VK_NULL_HANDLE) {
        vkDestroyDescriptorSetLayout(m_device, m_materialSetLayout, nullptr);
        m_materialSetLayout = VK_NULL_HANDLE;
    }
}

// 确保默认白纹理存在（baseColor / metallicRoughness / occlusion / emissive 无贴图时使用）。
TextureHandle VKRender3D::EnsureDefaultWhiteTexture() {
    if (m_defaultWhiteHandle != 0 && IsTextureValid(m_defaultWhiteHandle)) {
        return m_defaultWhiteHandle;
    }
    const uint8_t white[4] = { 255, 255, 255, 255 };
    TextureDesc desc;
    desc.width = 1;
    desc.height = 1;
    desc.semantic = TextureSemantic::Color;
    desc.generateMipmaps = false;
    desc.pixels = white;
    desc.sizeBytes = sizeof(white);
    desc.debugName = "default-white";
    desc.cacheKey = "builtin:default-white";
    m_defaultWhiteHandle = CreateTexture(desc);
    return m_defaultWhiteHandle;
}

// 确保默认平面法线纹理存在（无 normal 贴图时使用）。切线空间法线 (0,0,1) -> (0.5,0.5,1)。
TextureHandle VKRender3D::EnsureDefaultFlatNormalTexture() {
    if (m_defaultFlatNormalHandle != 0 && IsTextureValid(m_defaultFlatNormalHandle)) {
        return m_defaultFlatNormalHandle;
    }
    const uint8_t flatNormal[4] = { 128, 128, 255, 255 };
    TextureDesc desc;
    desc.width = 1;
    desc.height = 1;
    desc.semantic = TextureSemantic::Normal;
    desc.generateMipmaps = false;
    desc.pixels = flatNormal;
    desc.sizeBytes = sizeof(flatNormal);
    desc.debugName = "default-flat-normal";
    desc.cacheKey = "builtin:default-flat-normal";
    m_defaultFlatNormalHandle = CreateTexture(desc);
    return m_defaultFlatNormalHandle;
}

// 取得（或惰性分配）某组纹理对应的 set 1 描述符集。
VkDescriptorSet VKRender3D::MaterialDescriptorSetFor(const MaterialTextureSet& textures) {
    if (m_device == VK_NULL_HANDLE || m_materialSetLayout == VK_NULL_HANDLE) return VK_NULL_HANDLE;

    // 把每个槽解析为有效纹理句柄；缺失的槽用默认贴图补齐。
    TextureHandle resolved[kMaterialTextureSlotCount];
    resolved[0] = (textures.baseColor != 0 && IsTextureValid(textures.baseColor))
        ? textures.baseColor : EnsureDefaultWhiteTexture();
    resolved[1] = (textures.metallicRoughness != 0 && IsTextureValid(textures.metallicRoughness))
        ? textures.metallicRoughness : EnsureDefaultWhiteTexture();
    resolved[2] = (textures.normal != 0 && IsTextureValid(textures.normal))
        ? textures.normal : EnsureDefaultFlatNormalTexture();
    resolved[3] = (textures.occlusion != 0 && IsTextureValid(textures.occlusion))
        ? textures.occlusion : EnsureDefaultWhiteTexture();
    resolved[4] = (textures.emissive != 0 && IsTextureValid(textures.emissive))
        ? textures.emissive : EnsureDefaultWhiteTexture();

    // 缓存键 = 5 个句柄的组合；任一句柄变化即视为不同描述符集。
    std::string cacheKey;
    cacheKey.reserve(kMaterialTextureSlotCount * 12);
    for (uint32_t slot = 0; slot < kMaterialTextureSlotCount; ++slot) {
        cacheKey += std::to_string(resolved[slot]);
        cacheKey.push_back('|');
    }

    const auto found = m_materialDescriptorSets.find(cacheKey);
    if (found != m_materialDescriptorSets.end()) {
        return found->second.set;
    }

    VkDescriptorSetAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    allocInfo.descriptorPool = m_materialDescriptorPool;
    allocInfo.descriptorSetCount = 1;
    allocInfo.pSetLayouts = &m_materialSetLayout;

    VkDescriptorSet set = VK_NULL_HANDLE;
    if (vkAllocateDescriptorSets(m_device, &allocInfo, &set) != VK_SUCCESS) {
        return VK_NULL_HANDLE;
    }

    VkDescriptorImageInfo imageInfos[kMaterialTextureSlotCount]{};
    VkWriteDescriptorSet writes[kMaterialTextureSlotCount]{};
    bool valid = true;
    for (uint32_t slot = 0; slot < kMaterialTextureSlotCount; ++slot) {
        const VkImageView view = GetTextureImageView(resolved[slot]);
        const VkSampler sampler = GetTextureSampler(resolved[slot]);
        if (view == VK_NULL_HANDLE || sampler == VK_NULL_HANDLE) {
            valid = false;
            break;
        }
        imageInfos[slot].sampler = sampler;
        imageInfos[slot].imageView = view;
        imageInfos[slot].imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

        writes[slot].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[slot].dstSet = set;
        writes[slot].dstBinding = slot;
        writes[slot].dstArrayElement = 0;
        writes[slot].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        writes[slot].descriptorCount = 1;
        writes[slot].pImageInfo = &imageInfos[slot];
    }
    if (!valid) {
        vkFreeDescriptorSets(m_device, m_materialDescriptorPool, 1, &set);
        return VK_NULL_HANDLE;
    }
    vkUpdateDescriptorSets(m_device, kMaterialTextureSlotCount, writes, 0, nullptr);

    MaterialDescriptorSetEntry entry;
    entry.set = set;
    for (uint32_t slot = 0; slot < kMaterialTextureSlotCount; ++slot) {
        entry.handles[slot] = resolved[slot];
    }
    m_materialDescriptorSets.emplace(cacheKey, entry);
    return set;
}

// 创建并映射 UBO。
bool VKRender3D::CreateUniformBuffers() {
    VkDeviceSize bufferSize = sizeof(UniformBufferObject3D);

    m_uniformBuffers.resize(MAX_FRAMES_IN_FLIGHT);
    m_uniformBuffersMemory.resize(MAX_FRAMES_IN_FLIGHT);
    m_uniformBuffersMapped.resize(MAX_FRAMES_IN_FLIGHT);

    for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
        VkBufferCreateInfo bufferInfo{};
        bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        bufferInfo.size = bufferSize;
        bufferInfo.usage = VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT;
        bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

        if (vkCreateBuffer(m_device, &bufferInfo, nullptr, &m_uniformBuffers[i]) != VK_SUCCESS) {
            std::cerr << "3D: 创建统一缓冲区失败" << std::endl;
            return false;
        }

        VkMemoryRequirements memRequirements;
        vkGetBufferMemoryRequirements(m_device, m_uniformBuffers[i], &memRequirements);

        VkMemoryAllocateInfo allocInfo{};
        allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        allocInfo.allocationSize = memRequirements.size;
        allocInfo.memoryTypeIndex = FindMemoryType(memRequirements.memoryTypeBits,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);

        if (vkAllocateMemory(m_device, &allocInfo, nullptr, &m_uniformBuffersMemory[i]) != VK_SUCCESS) {
            std::cerr << "3D: 分配统一缓冲区内存失败" << std::endl;
            return false;
        }

        vkBindBufferMemory(m_device, m_uniformBuffers[i], m_uniformBuffersMemory[i], 0);

        vkMapMemory(m_device, m_uniformBuffersMemory[i], 0, bufferSize, 0, &m_uniformBuffersMapped[i]);

        UniformBufferObject3D ubo{};
        InitIdentityMatrix(ubo.model);
        InitIdentityMatrix(ubo.view);
        InitIdentityMatrix(ubo.proj);
        memcpy(m_uniformBuffersMapped[i], &ubo, sizeof(ubo));
    }

    return true;
}

// 创建描述符池。
bool VKRender3D::CreateDescriptorPool() {
    VkDescriptorPoolSize poolSizes[2]{};
    poolSizes[0].type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    poolSizes[0].descriptorCount = static_cast<uint32_t>(MAX_FRAMES_IN_FLIGHT * 2);
    poolSizes[1].type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    poolSizes[1].descriptorCount = static_cast<uint32_t>(MAX_FRAMES_IN_FLIGHT * 2);

    VkDescriptorPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    poolInfo.poolSizeCount = 2;
    poolInfo.pPoolSizes = poolSizes;
    poolInfo.maxSets = static_cast<uint32_t>(MAX_FRAMES_IN_FLIGHT * 2);

    if (vkCreateDescriptorPool(m_device, &poolInfo, nullptr, &m_descriptorPool) != VK_SUCCESS) {
        std::cerr << "3D: 创建描述符池失败" << std::endl;
        return false;
    }
    return true;
}

// 分配并写入描述符集。
bool VKRender3D::CreateDescriptorSets() {
    std::vector<VkDescriptorSetLayout> layouts(MAX_FRAMES_IN_FLIGHT * 2, m_descriptorSetLayout);

    VkDescriptorSetAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    allocInfo.descriptorPool = m_descriptorPool;
    allocInfo.descriptorSetCount = static_cast<uint32_t>(layouts.size());
    allocInfo.pSetLayouts = layouts.data();

    std::vector<VkDescriptorSet> allocated(layouts.size());
    if (vkAllocateDescriptorSets(m_device, &allocInfo, allocated.data()) != VK_SUCCESS) {
        std::cerr << "3D: 分配描述符集失败" << std::endl;
        return false;
    }

    m_descriptorSets.assign(allocated.begin(), allocated.begin() + MAX_FRAMES_IN_FLIGHT);
    m_shadowDescriptorSets.assign(allocated.begin() + MAX_FRAMES_IN_FLIGHT, allocated.end());

    auto writeUbo = [this](VkDescriptorSet set, VkBuffer buffer) {
        VkDescriptorBufferInfo bufferInfo{};
        bufferInfo.buffer = buffer;
        bufferInfo.offset = 0;
        bufferInfo.range = sizeof(UniformBufferObject3D);

        VkWriteDescriptorSet descriptorWrite{};
        descriptorWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        descriptorWrite.dstSet = set;
        descriptorWrite.dstBinding = 0;
        descriptorWrite.dstArrayElement = 0;
        descriptorWrite.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        descriptorWrite.descriptorCount = 1;
        descriptorWrite.pBufferInfo = &bufferInfo;
        vkUpdateDescriptorSets(m_device, 1, &descriptorWrite, 0, nullptr);
    };

    for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
        writeUbo(m_descriptorSets[i], m_uniformBuffers[i]);
        writeUbo(m_shadowDescriptorSets[i], m_uniformBuffers[i]);
    }

    UpdateShadowDescriptors();
    return true;
}

// 销毁 UBO 及其内存。
void VKRender3D::DestroyUniformBuffers() {
    for (size_t i = 0; i < m_uniformBuffers.size(); i++) {
        if (m_uniformBuffersMapped[i] != nullptr) {
            vkUnmapMemory(m_device, m_uniformBuffersMemory[i]);
            m_uniformBuffersMapped[i] = nullptr;
        }
        if (m_uniformBuffers[i] != VK_NULL_HANDLE) {
            vkDestroyBuffer(m_device, m_uniformBuffers[i], nullptr);
            m_uniformBuffers[i] = VK_NULL_HANDLE;
        }
        if (m_uniformBuffersMemory[i] != VK_NULL_HANDLE) {
            vkFreeMemory(m_device, m_uniformBuffersMemory[i], nullptr);
            m_uniformBuffersMemory[i] = VK_NULL_HANDLE;
        }
    }
    m_uniformBuffers.clear();
    m_uniformBuffersMemory.clear();
    m_uniformBuffersMapped.clear();
}

// 按当前相机与显示选项写入 UBO。
void VKRender3D::UpdateUniformBuffer(uint32_t currentImage) {
    float aspect = (float)m_framebufferWidth / (float)m_framebufferHeight;

    const glm::vec3 eye(0.0f, 0.0f, m_orbitDistance);
    const glm::mat4 model = glm::translate(glm::mat4(1.0f), m_panOffset)
        * glm::mat4_cast(m_modelRotation)
        * glm::translate(glm::mat4(1.0f), -m_orbitCenter);
    const glm::mat4 view = glm::lookAt(
        eye, glm::vec3(0.0f), glm::vec3(0.0f, 1.0f, 0.0f));

    const float verticalFov = glm::radians(45.0f);
    glm::mat4 proj;
    if (m_orthographicEnabled) {
        const float halfHeight = m_orbitDistance * std::tan(verticalFov * 0.5f);
        const float halfWidth = halfHeight * aspect;
        proj = glm::orthoRH_ZO(-halfWidth, halfWidth,
                               -halfHeight, halfHeight,
                               0.1f, 100.0f);
    } else {
        proj = glm::perspectiveRH_ZO(verticalFov, aspect, 0.1f, 100.0f);
    }
    proj[1][1] *= -1;

    m_frameInvViewProj[currentImage] = glm::inverse(proj * view);
    m_frameRenderToSource[currentImage] = m_normalizedToWorld * glm::inverse(model);

    UniformBufferObject3D ubo{};
    memcpy(ubo.model, glm::value_ptr(model), sizeof(float) * 16);
    memcpy(ubo.view, glm::value_ptr(view), sizeof(float) * 16);
    memcpy(ubo.proj, glm::value_ptr(proj), sizeof(float) * 16);
    ubo.displayOptions[0] = m_grayEnabled ? 1.0f : 0.0f;
    ubo.displayOptions[1] = m_dyeEnabled ? 1.0f : 0.0f;
    ubo.displayOptions[2] = m_lightAnalysisEnabled ? 1.0f : 0.0f;
    ubo.displayOptions[3] = m_sunAboveHorizon ? 1.0f : 0.0f;
    ubo.sunDirection[0] = m_sunDirection.x;
    ubo.sunDirection[1] = m_sunDirection.y;
    ubo.sunDirection[2] = m_sunDirection.z;
    ubo.sunDirection[3] = 0.0f;
    // 阴影贴图与采样矩阵必须成对更新：只在阴影通道中重算并锁定光源矩阵，
    // 主绘制复用同一矩阵。否则贴图每 100ms 才刷新，而矩阵每帧随太阳变化，
    // 光照变化期间会出现阴影游动/抽搐。
    if (m_shadowPassActive) {
        m_shadowLightViewProj = ComputeLightViewProj();
        m_shadowMatrixValid = true;
    }
    m_lightViewProj = m_shadowLightViewProj;
    memcpy(ubo.lightViewProj, glm::value_ptr(m_lightViewProj), sizeof(float) * 16);
    ubo.shadowOptions[0] = ShouldRenderShadows() ? 1.0f : 0.0f;
    ubo.shadowOptions[1] = 0.0f;
    ubo.shadowOptions[2] = m_wireframeMode ? 1.0f : 0.0f;
    // Vulkan 窗口 Y 向下：告知着色器把 dFdy 取反，使几何法线与 OpenGL 一致。
    ubo.shadowOptions[3] = 1.0f;
    // PBR 视线向量：把相机（世界空间）变换到物体空间，与物体空间法线/太阳方向一致。
    const glm::vec3 cameraObject = glm::vec3(glm::inverse(model) * glm::vec4(eye, 1.0f));
    ubo.cameraObjectPosition[0] = cameraObject.x;
    ubo.cameraObjectPosition[1] = cameraObject.y;
    ubo.cameraObjectPosition[2] = cameraObject.z;
    ubo.cameraObjectPosition[3] = 0.0f;
    // 1.7：半球环境光（线性）。
    ubo.ambientSkyColor[0] = m_ambientSkyColor.x;
    ubo.ambientSkyColor[1] = m_ambientSkyColor.y;
    ubo.ambientSkyColor[2] = m_ambientSkyColor.z;
    ubo.ambientSkyColor[3] = m_ambientIntensity;
    ubo.ambientGroundColor[0] = m_ambientGroundColor.x;
    ubo.ambientGroundColor[1] = m_ambientGroundColor.y;
    ubo.ambientGroundColor[2] = m_ambientGroundColor.z;
    ubo.ambientGroundColor[3] = 0.0f;
    // 1.7：阴影偏移/PCF/法线偏移。
    ubo.shadowParams[0] = m_shadowBias;
    ubo.shadowParams[1] = static_cast<float>(m_shadowPcfMode);
    ubo.shadowParams[2] = (m_shadowNormalOffsetScale > 0.0f && m_allocatedShadowTextureSize > 0)
        ? (2.0f * glm::length(glm::vec3(m_shadowBoundsMax - m_shadowBoundsMin))
            / static_cast<float>(m_allocatedShadowTextureSize)) * m_shadowNormalOffsetScale
        : 0.0f;
    ubo.shadowParams[3] = 0.0f;
    // 1.7：调试视图 + 线性深度归一化范围。
    ubo.debugOptions[0] = static_cast<float>(m_debugView);
    ubo.debugOptions[1] = 0.0f;
    ubo.debugOptions[2] = 0.0f;
    ubo.debugOptions[3] = 0.0f;
    ubo.depthRange[0] = 0.1f;   // 近平面（与 UpdateUniformBuffer 中的 proj 一致）
    ubo.depthRange[1] = 100.0f; // 远平面
    ubo.depthRange[2] = 0.0f;
    ubo.depthRange[3] = 0.0f;
    memcpy(m_uniformBuffersMapped[currentImage], &ubo, sizeof(ubo));
}

// 开关线框模式。
void VKRender3D::SetWireframeEnabled(bool enabled) {
    m_wireframeMode = enabled;
}

// 开关灰度显示。
void VKRender3D::SetGrayEnabled(bool enabled) {
    m_grayEnabled = enabled;
}

// 开关染色。
void VKRender3D::SetDyeEnabled(bool enabled) {
    m_dyeEnabled = enabled;
}

// 开关光照分析。
void VKRender3D::SetLightAnalysisEnabled(bool enabled) {
    m_lightAnalysisEnabled = enabled;
    if (!m_initialized) return;
    if (!enabled) {
        WaitForIdle();
        DestroyShadowMap();
        m_shadowMapReady = false;
        m_allocatedShadowTextureSize = 0;
        UpdateShadowDescriptors();
        return;
    }
    EnsureShadowMapForAnalysis();
}

// PBR 调试覆盖（1.5）：覆盖材质的 metallic。
void VKRender3D::SetMetallicOverride(bool enabled, float value) {
    m_metallicOverrideEnabled = enabled;
    m_metallicOverride = value;
}

// PBR 调试覆盖（1.5）：覆盖材质的 roughness。
void VKRender3D::SetRoughnessOverride(bool enabled, float value) {
    m_roughnessOverrideEnabled = enabled;
    m_roughnessOverride = value;
}

// PBR 调试覆盖（1.5）：覆盖材质的自发光倍率。
void VKRender3D::SetEmissiveOverride(bool enabled, float value) {
    m_emissiveOverrideEnabled = enabled;
    m_emissiveOverride = value;
}

// 设置阴影贴图边长。
void VKRender3D::SetShadowTextureSize(uint32_t size) {
    m_shadowTextureSize = size;
    if (m_initialized && m_lightAnalysisEnabled) {
        EnsureShadowMapForAnalysis();
    }
}

// 获取阴影贴图尺寸。
uint32_t VKRender3D::GetShadowTextureSize() const {
    return m_shadowTextureSize;
}

// 查询阴影贴图是否就绪。
bool VKRender3D::IsShadowMapReady() const {
    return m_shadowMapReady;
}

// 读取并清除阴影贴图状态。
std::string VKRender3D::TakeShadowMapStatus() {
    std::string message = std::move(m_shadowMapStatus);
    m_shadowMapStatus.clear();
    return message;
}

// 设置阴影场景包围盒。
void VKRender3D::SetShadowSceneBounds(const Vec3& boundsMin, const Vec3& boundsMax, bool valid) {
    m_shadowBoundsValid = valid;
    m_shadowBoundsMin = glm::vec3(boundsMin.x, boundsMin.y, boundsMin.z);
    m_shadowBoundsMax = glm::vec3(boundsMax.x, boundsMax.y, boundsMax.z);
}

// 记录阴影贴图状态文本。
void VKRender3D::SetShadowMapStatus(const std::string& message) {
    m_shadowMapStatus = message;
}

// 判断当前是否应渲染阴影。
bool VKRender3D::ShouldRenderShadows() const {
    return m_lightAnalysisEnabled && m_sunAboveHorizon && m_shadowMapReady
        && m_shadowMatrixValid && !m_shadowPassActive;
}

// 获取阴影图形管线。
VkPipeline VKRender3D::GetShadowPipeline() const {
    return m_shadowPipeline;
}

// 计算光源视图投影矩阵。
glm::mat4 VKRender3D::ComputeLightViewProj() const {
    glm::vec3 boundsMin = m_shadowBoundsMin;
    glm::vec3 boundsMax = m_shadowBoundsMax;
    if (!m_shadowBoundsValid) {
        boundsMin = glm::vec3(-1.0f);
        boundsMax = glm::vec3(1.0f);
    }

    glm::vec3 center = (boundsMin + boundsMax) * 0.5f;
    glm::vec3 extent = (boundsMax - boundsMin) * 0.5f * 1.1f;
    extent = glm::max(extent, glm::vec3(0.1f));

    glm::vec3 sun = m_sunDirection;
    const float sunLength = glm::length(sun);
    if (sunLength <= 1.0e-6f) {
        sun = glm::vec3(0.0f, 0.0f, 1.0f);
    } else {
        sun /= sunLength;
    }

    // 选择与太阳最不平行的世界轴作为 up，避免太阳接近天顶时 lookAt 退化，
    // 造成每帧 roll 剧烈摆动（阴影抖动）。此选择保证 |dot(sun, up)| <= 1/sqrt(3)。
    const glm::vec3 worldAxes[3] = {
        glm::vec3(1.0f, 0.0f, 0.0f),
        glm::vec3(0.0f, 1.0f, 0.0f),
        glm::vec3(0.0f, 0.0f, 1.0f)
    };
    glm::vec3 up = worldAxes[0];
    float minAlignment = std::abs(glm::dot(sun, worldAxes[0]));
    for (int axis = 1; axis < 3; ++axis) {
        const float alignment = std::abs(glm::dot(sun, worldAxes[axis]));
        if (alignment < minAlignment) {
            minAlignment = alignment;
            up = worldAxes[axis];
        }
    }

    const float radius = glm::length(extent);
    const glm::vec3 eye = center + sun * (radius + 0.1f);
    const glm::mat4 lightView = glm::lookAt(eye, center, up);

    const glm::vec3 paddedMin = center - extent;
    const glm::vec3 paddedMax = center + extent;
    const glm::vec3 corners[8] = {
        { paddedMin.x, paddedMin.y, paddedMin.z },
        { paddedMax.x, paddedMin.y, paddedMin.z },
        { paddedMin.x, paddedMax.y, paddedMin.z },
        { paddedMax.x, paddedMax.y, paddedMin.z },
        { paddedMin.x, paddedMin.y, paddedMax.z },
        { paddedMax.x, paddedMin.y, paddedMax.z },
        { paddedMin.x, paddedMax.y, paddedMax.z },
        { paddedMax.x, paddedMax.y, paddedMax.z }
    };

    glm::vec3 viewMin(std::numeric_limits<float>::max());
    glm::vec3 viewMax(std::numeric_limits<float>::lowest());
    for (const glm::vec3& corner : corners) {
        const glm::vec3 viewPos = glm::vec3(lightView * glm::vec4(corner, 1.0f));
        viewMin = glm::min(viewMin, viewPos);
        viewMax = glm::max(viewMax, viewPos);
    }

    float zNear = std::max(0.01f, -viewMax.z);
    float zFar = std::max(zNear + 0.01f, -viewMin.z);

    // 1.7：光源正交投影 texel 对齐。把光源空间投影窗口的 x/y 中心量化到阴影贴图纹素
    // 网格上，消除物体/太阳移动时的阴影游动与闪烁。窗口边长取光照空间 AABB 的较大边，
    // 保持每帧稳定。
    const uint32_t shadowSize = (m_allocatedShadowTextureSize != 0)
        ? m_allocatedShadowTextureSize : m_shadowTextureSize;
    const float halfSize = std::max(viewMax.x - viewMin.x, viewMax.y - viewMin.y) * 0.5f;
    const float texelWorld = (shadowSize != 0 && halfSize > 1.0e-6f)
        ? (2.0f * halfSize / static_cast<float>(shadowSize)) : 0.0f;
    float centerX = (viewMin.x + viewMax.x) * 0.5f;
    float centerY = (viewMin.y + viewMax.y) * 0.5f;
    if (texelWorld > 0.0f) {
        centerX = std::floor(centerX / texelWorld) * texelWorld;
        centerY = std::floor(centerY / texelWorld) * texelWorld;
    }

    glm::mat4 lightProj = glm::orthoRH_ZO(
        centerX - halfSize, centerX + halfSize,
        centerY - halfSize, centerY + halfSize,
        zNear, zFar);
    lightProj[1][1] *= -1;
    return lightProj * lightView;
}

// 把阴影贴图写入描述符集。
void VKRender3D::UpdateShadowDescriptors() {
    if (m_device == VK_NULL_HANDLE || m_shadowSampler == VK_NULL_HANDLE) return;

    auto writeImage = [this](VkDescriptorSet set, VkImageView view) {
        if (set == VK_NULL_HANDLE || view == VK_NULL_HANDLE) return;
        VkDescriptorImageInfo imageInfo{};
        imageInfo.sampler = m_shadowSampler;
        imageInfo.imageView = view;
        imageInfo.imageLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;

        VkWriteDescriptorSet descriptorWrite{};
        descriptorWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        descriptorWrite.dstSet = set;
        descriptorWrite.dstBinding = 1;
        descriptorWrite.dstArrayElement = 0;
        descriptorWrite.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        descriptorWrite.descriptorCount = 1;
        descriptorWrite.pImageInfo = &imageInfo;
        vkUpdateDescriptorSets(m_device, 1, &descriptorWrite, 0, nullptr);
    };

    VkImageView colorView = VK_NULL_HANDLE;
    if (m_shadowMapReady && m_sunAboveHorizon && m_shadowView != VK_NULL_HANDLE) {
        colorView = m_shadowView;
    } else if (m_dummyShadowReady) {
        colorView = m_dummyShadowView;
    }
    for (size_t i = 0; i < m_descriptorSets.size(); i++) {
        writeImage(m_descriptorSets[i], colorView);
    }
    if (m_dummyShadowReady) {
        for (size_t i = 0; i < m_shadowDescriptorSets.size(); i++) {
            writeImage(m_shadowDescriptorSets[i], m_dummyShadowView);
        }
    }
}

// 转换深度图像布局。
void VKRender3D::TransitionDepthImage(VkImage image, VkImageLayout oldLayout, VkImageLayout newLayout,
                                          VkAccessFlags srcAccess, VkAccessFlags dstAccess,
                                          VkPipelineStageFlags srcStage, VkPipelineStageFlags dstStage) {
    VkCommandBuffer cmd = BeginSingleTimeCommands();
    VkImageMemoryBarrier barrier{};
    barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barrier.oldLayout = oldLayout;
    barrier.newLayout = newLayout;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = image;
    barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
    if (HasStencilComponent(m_shadowDepthFormat)) {
        barrier.subresourceRange.aspectMask |= VK_IMAGE_ASPECT_STENCIL_BIT;
    }
    barrier.subresourceRange.baseMipLevel = 0;
    barrier.subresourceRange.levelCount = 1;
    barrier.subresourceRange.baseArrayLayer = 0;
    barrier.subresourceRange.layerCount = 1;
    barrier.srcAccessMask = srcAccess;
    barrier.dstAccessMask = dstAccess;
    vkCmdPipelineBarrier(cmd, srcStage, dstStage, 0, 0, nullptr, 0, nullptr, 1, &barrier);
    EndSingleTimeCommands(cmd);
}

// 创建阴影比较采样器。
bool VKRender3D::CreateShadowSampler() {
    VkSamplerCreateInfo samplerInfo{};
    samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    samplerInfo.magFilter = VK_FILTER_NEAREST;
    samplerInfo.minFilter = VK_FILTER_NEAREST;
    samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
    samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
    samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
    samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
    samplerInfo.compareEnable = VK_TRUE;
    samplerInfo.compareOp = VK_COMPARE_OP_LESS;
    samplerInfo.borderColor = VK_BORDER_COLOR_FLOAT_OPAQUE_WHITE;
    samplerInfo.maxAnisotropy = 1.0f;
    if (vkCreateSampler(m_device, &samplerInfo, nullptr, &m_shadowSampler) != VK_SUCCESS) {
        std::cerr << "3D: 创建阴影采样器失败" << std::endl;
        return false;
    }
    return true;
}

// 创建 1x1 占位阴影贴图。
bool VKRender3D::CreateDummyShadowMap() {
    VkFormat depthFormat = VK_FORMAT_UNDEFINED;
    try {
        depthFormat = FindShadowDepthFormat();
    } catch (...) {
        return false;
    }
    m_shadowDepthFormat = depthFormat;
    VkImageCreateInfo imageInfo{};
    imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    imageInfo.imageType = VK_IMAGE_TYPE_2D;
    imageInfo.extent = { 1, 1, 1 };
    imageInfo.mipLevels = 1;
    imageInfo.arrayLayers = 1;
    imageInfo.format = depthFormat;
    imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
    imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    imageInfo.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT |
                      VK_IMAGE_USAGE_SAMPLED_BIT |
                      VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
    imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    if (vkCreateImage(m_device, &imageInfo, nullptr, &m_dummyShadowImage) != VK_SUCCESS) {
        return false;
    }

    VkMemoryRequirements memRequirements;
    vkGetImageMemoryRequirements(m_device, m_dummyShadowImage, &memRequirements);
    VkMemoryAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocInfo.allocationSize = memRequirements.size;
    try {
        allocInfo.memoryTypeIndex = FindMemoryType(memRequirements.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    } catch (...) {
        vkDestroyImage(m_device, m_dummyShadowImage, nullptr);
        m_dummyShadowImage = VK_NULL_HANDLE;
        return false;
    }
    if (vkAllocateMemory(m_device, &allocInfo, nullptr, &m_dummyShadowMemory) != VK_SUCCESS) {
        vkDestroyImage(m_device, m_dummyShadowImage, nullptr);
        m_dummyShadowImage = VK_NULL_HANDLE;
        return false;
    }
    vkBindImageMemory(m_device, m_dummyShadowImage, m_dummyShadowMemory, 0);

    VkImageViewCreateInfo viewInfo{};
    viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    viewInfo.image = m_dummyShadowImage;
    viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
    viewInfo.format = depthFormat;
    viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
    viewInfo.subresourceRange.levelCount = 1;
    viewInfo.subresourceRange.layerCount = 1;
    if (vkCreateImageView(m_device, &viewInfo, nullptr, &m_dummyShadowView) != VK_SUCCESS) {
        DestroyDummyShadowMap();
        return false;
    }
    return true;
}

// 保证占位阴影图布局可用。
bool VKRender3D::EnsureDummyShadowReady() {
    if (m_dummyShadowReady || m_dummyShadowImage == VK_NULL_HANDLE) return m_dummyShadowReady;
    TransitionDepthImage(m_dummyShadowImage,
        VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
        0, VK_ACCESS_TRANSFER_WRITE_BIT,
        VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT);

    VkCommandBuffer cmd = BeginSingleTimeCommands();
    VkClearDepthStencilValue clearValue{};
    clearValue.depth = 1.0f;
    clearValue.stencil = 0;
    VkImageSubresourceRange range{};
    range.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
    if (HasStencilComponent(m_shadowDepthFormat)) {
        range.aspectMask |= VK_IMAGE_ASPECT_STENCIL_BIT;
    }
    range.levelCount = 1;
    range.layerCount = 1;
    vkCmdClearDepthStencilImage(cmd, m_dummyShadowImage, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &clearValue, 1, &range);
    EndSingleTimeCommands(cmd);

    TransitionDepthImage(m_dummyShadowImage,
        VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL,
        VK_ACCESS_TRANSFER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT,
        VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT);
    m_dummyShadowReady = true;
    UpdateShadowDescriptors();
    return true;
}

// 创建仅深度的阴影渲染通道。
bool VKRender3D::CreateShadowRenderPass() {
    VkFormat depthFormat = m_shadowDepthFormat;
    if (depthFormat == VK_FORMAT_UNDEFINED) {
        try {
            depthFormat = FindShadowDepthFormat();
        } catch (...) {
            return false;
        }
        m_shadowDepthFormat = depthFormat;
    }
    VkAttachmentDescription depthAttachment{};
    depthAttachment.format = depthFormat;
    depthAttachment.samples = VK_SAMPLE_COUNT_1_BIT;
    depthAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    depthAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    depthAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    depthAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    depthAttachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    depthAttachment.finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;

    VkAttachmentReference depthAttachmentRef{};
    depthAttachmentRef.attachment = 0;
    depthAttachmentRef.layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

    VkSubpassDescription subpass{};
    subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.pDepthStencilAttachment = &depthAttachmentRef;

    VkSubpassDependency dependencies[2]{};
    dependencies[0].srcSubpass = VK_SUBPASS_EXTERNAL;
    dependencies[0].dstSubpass = 0;
    dependencies[0].srcStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
    dependencies[0].dstStageMask = VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
    dependencies[0].srcAccessMask = VK_ACCESS_SHADER_READ_BIT;
    dependencies[0].dstAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;

    dependencies[1].srcSubpass = 0;
    dependencies[1].dstSubpass = VK_SUBPASS_EXTERNAL;
    dependencies[1].srcStageMask = VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
    dependencies[1].dstStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
    dependencies[1].srcAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
    dependencies[1].dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

    VkRenderPassCreateInfo renderPassInfo{};
    renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    renderPassInfo.attachmentCount = 1;
    renderPassInfo.pAttachments = &depthAttachment;
    renderPassInfo.subpassCount = 1;
    renderPassInfo.pSubpasses = &subpass;
    renderPassInfo.dependencyCount = 2;
    renderPassInfo.pDependencies = dependencies;
    if (vkCreateRenderPass(m_device, &renderPassInfo, nullptr, &m_shadowRenderPass) != VK_SUCCESS) {
        std::cerr << "3D: 创建阴影渲染通道失败" << std::endl;
        return false;
    }
    return true;
}

// 创建阴影图形管线。
bool VKRender3D::CreateShadowPipeline() {
    const std::string vertShaderPath = AssetPath::ShaderFile("3d_shadow_vert.spv");
    const std::string fragShaderPath = AssetPath::ShaderFile("3d_shadow_frag.spv");
    auto vertShaderCode = ReadShaderFile(vertShaderPath);
    auto fragShaderCode = ReadShaderFile(fragShaderPath);
    VkShaderModule vertShaderModule = CreateShaderModuleHelper(
        vertShaderCode, vertShaderPath);
    VkShaderModule fragShaderModule = CreateShaderModuleHelper(
        fragShaderCode, fragShaderPath);

    VkPipelineShaderStageCreateInfo vertStage{};
    vertStage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    vertStage.stage = VK_SHADER_STAGE_VERTEX_BIT;
    vertStage.module = vertShaderModule;
    vertStage.pName = "main";

    VkPipelineShaderStageCreateInfo fragStage{};
    fragStage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    fragStage.stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    fragStage.module = fragShaderModule;
    fragStage.pName = "main";

    VkPipelineShaderStageCreateInfo shaderStages[] = { vertStage, fragStage };

    auto bindingDescription = Vertex3D::GetBindingDescription();
    auto attributeDescriptions = Vertex3D::GetAttributeDescriptions();
    VkPipelineVertexInputStateCreateInfo vertexInputInfo{};
    vertexInputInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    vertexInputInfo.vertexBindingDescriptionCount = 1;
    vertexInputInfo.pVertexBindingDescriptions = &bindingDescription;
    vertexInputInfo.vertexAttributeDescriptionCount = static_cast<uint32_t>(attributeDescriptions.size());
    vertexInputInfo.pVertexAttributeDescriptions = attributeDescriptions.data();

    VkPipelineInputAssemblyStateCreateInfo inputAssembly{};
    inputAssembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

    std::vector<VkDynamicState> dynamicStates = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };
    VkPipelineDynamicStateCreateInfo dynamicState{};
    dynamicState.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    dynamicState.dynamicStateCount = static_cast<uint32_t>(dynamicStates.size());
    dynamicState.pDynamicStates = dynamicStates.data();

    VkPipelineViewportStateCreateInfo viewportState{};
    viewportState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    viewportState.viewportCount = 1;
    viewportState.scissorCount = 1;

    VkPipelineRasterizationStateCreateInfo rasterizer{};
    rasterizer.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    rasterizer.polygonMode = VK_POLYGON_MODE_FILL;
    rasterizer.lineWidth = 1.0f;
    rasterizer.cullMode = VK_CULL_MODE_NONE;
    rasterizer.frontFace = VK_FRONT_FACE_CLOCKWISE;
    rasterizer.depthBiasEnable = VK_TRUE;
    rasterizer.depthBiasConstantFactor = 1.25f;
    rasterizer.depthBiasSlopeFactor = 1.75f;

    VkPipelineMultisampleStateCreateInfo multisampling{};
    multisampling.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    multisampling.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

    VkPipelineDepthStencilStateCreateInfo depthStencil{};
    depthStencil.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    depthStencil.depthTestEnable = VK_TRUE;
    depthStencil.depthWriteEnable = VK_TRUE;
    depthStencil.depthCompareOp = VK_COMPARE_OP_LESS;

    VkPipelineColorBlendStateCreateInfo colorBlending{};
    colorBlending.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;

    VkGraphicsPipelineCreateInfo pipelineInfo{};
    pipelineInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    pipelineInfo.stageCount = 2;
    pipelineInfo.pStages = shaderStages;
    pipelineInfo.pVertexInputState = &vertexInputInfo;
    pipelineInfo.pInputAssemblyState = &inputAssembly;
    pipelineInfo.pViewportState = &viewportState;
    pipelineInfo.pRasterizationState = &rasterizer;
    pipelineInfo.pMultisampleState = &multisampling;
    pipelineInfo.pDepthStencilState = &depthStencil;
    pipelineInfo.pColorBlendState = &colorBlending;
    pipelineInfo.pDynamicState = &dynamicState;
    pipelineInfo.layout = m_pipelineLayout;
    pipelineInfo.renderPass = m_shadowRenderPass;
    pipelineInfo.subpass = 0;

    const VkResult result = vkCreateGraphicsPipelines(m_device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &m_shadowPipeline);
    vkDestroyShaderModule(m_device, fragShaderModule, nullptr);
    vkDestroyShaderModule(m_device, vertShaderModule, nullptr);
    if (result != VK_SUCCESS) {
        return Fail("3D: 创建阴影管线失败");
    }
    return true;
}

// 按给定边长创建阴影深度贴图。
bool VKRender3D::CreateShadowMap(uint32_t size) {
    VkPhysicalDeviceProperties properties{};
    vkGetPhysicalDeviceProperties(m_physicalDevice, &properties);
    if (size == 0 || size > properties.limits.maxImageDimension2D) {
        return false;
    }

    VkFormat depthFormat = m_shadowDepthFormat;
    if (depthFormat == VK_FORMAT_UNDEFINED) {
        try {
            depthFormat = FindShadowDepthFormat();
        } catch (...) {
            return false;
        }
        m_shadowDepthFormat = depthFormat;
    }
    VkImageCreateInfo imageInfo{};
    imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    imageInfo.imageType = VK_IMAGE_TYPE_2D;
    imageInfo.extent = { size, size, 1 };
    imageInfo.mipLevels = 1;
    imageInfo.arrayLayers = 1;
    imageInfo.format = depthFormat;
    imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
    imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    imageInfo.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT |
                      VK_IMAGE_USAGE_SAMPLED_BIT |
                      VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
    imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    if (vkCreateImage(m_device, &imageInfo, nullptr, &m_shadowImage) != VK_SUCCESS) {
        return false;
    }

    VkMemoryRequirements memRequirements;
    vkGetImageMemoryRequirements(m_device, m_shadowImage, &memRequirements);
    VkMemoryAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocInfo.allocationSize = memRequirements.size;
    try {
        allocInfo.memoryTypeIndex = FindMemoryType(memRequirements.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    } catch (...) {
        vkDestroyImage(m_device, m_shadowImage, nullptr);
        m_shadowImage = VK_NULL_HANDLE;
        return false;
    }
    if (vkAllocateMemory(m_device, &allocInfo, nullptr, &m_shadowMemory) != VK_SUCCESS) {
        vkDestroyImage(m_device, m_shadowImage, nullptr);
        m_shadowImage = VK_NULL_HANDLE;
        return false;
    }
    vkBindImageMemory(m_device, m_shadowImage, m_shadowMemory, 0);

    VkImageViewCreateInfo viewInfo{};
    viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    viewInfo.image = m_shadowImage;
    viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
    viewInfo.format = depthFormat;
    viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
    viewInfo.subresourceRange.levelCount = 1;
    viewInfo.subresourceRange.layerCount = 1;
    if (vkCreateImageView(m_device, &viewInfo, nullptr, &m_shadowView) != VK_SUCCESS) {
        DestroyShadowMap();
        return false;
    }

    VkFramebufferCreateInfo framebufferInfo{};
    framebufferInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
    framebufferInfo.renderPass = m_shadowRenderPass;
    framebufferInfo.attachmentCount = 1;
    framebufferInfo.pAttachments = &m_shadowView;
    framebufferInfo.width = size;
    framebufferInfo.height = size;
    framebufferInfo.layers = 1;
    if (vkCreateFramebuffer(m_device, &framebufferInfo, nullptr, &m_shadowFramebuffer) != VK_SUCCESS) {
        DestroyShadowMap();
        return false;
    }

    TransitionDepthImage(m_shadowImage,
        VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
        0, VK_ACCESS_TRANSFER_WRITE_BIT,
        VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT);
    VkCommandBuffer cmd = BeginSingleTimeCommands();
    VkClearDepthStencilValue clearValue{};
    clearValue.depth = 1.0f;
    VkImageSubresourceRange range{};
    range.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
    if (HasStencilComponent(depthFormat)) {
        range.aspectMask |= VK_IMAGE_ASPECT_STENCIL_BIT;
    }
    range.levelCount = 1;
    range.layerCount = 1;
    vkCmdClearDepthStencilImage(cmd, m_shadowImage, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &clearValue, 1, &range);
    EndSingleTimeCommands(cmd);
    TransitionDepthImage(m_shadowImage,
        VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL,
        VK_ACCESS_TRANSFER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT,
        VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT);
    return true;
}

// 销毁阴影贴图与帧缓冲。
void VKRender3D::DestroyShadowMap() {
    if (m_shadowFramebuffer != VK_NULL_HANDLE) {
        vkDestroyFramebuffer(m_device, m_shadowFramebuffer, nullptr);
        m_shadowFramebuffer = VK_NULL_HANDLE;
    }
    if (m_shadowView != VK_NULL_HANDLE) {
        vkDestroyImageView(m_device, m_shadowView, nullptr);
        m_shadowView = VK_NULL_HANDLE;
    }
    if (m_shadowMemory != VK_NULL_HANDLE) {
        vkFreeMemory(m_device, m_shadowMemory, nullptr);
        m_shadowMemory = VK_NULL_HANDLE;
    }
    if (m_shadowImage != VK_NULL_HANDLE) {
        vkDestroyImage(m_device, m_shadowImage, nullptr);
        m_shadowImage = VK_NULL_HANDLE;
    }
    m_shadowMapReady = false;
    m_allocatedShadowTextureSize = 0;
    m_shadowMatrixValid = false;
}

// 销毁占位阴影贴图。
void VKRender3D::DestroyDummyShadowMap() {
    if (m_dummyShadowView != VK_NULL_HANDLE) {
        vkDestroyImageView(m_device, m_dummyShadowView, nullptr);
        m_dummyShadowView = VK_NULL_HANDLE;
    }
    if (m_dummyShadowMemory != VK_NULL_HANDLE) {
        vkFreeMemory(m_device, m_dummyShadowMemory, nullptr);
        m_dummyShadowMemory = VK_NULL_HANDLE;
    }
    if (m_dummyShadowImage != VK_NULL_HANDLE) {
        vkDestroyImage(m_device, m_dummyShadowImage, nullptr);
        m_dummyShadowImage = VK_NULL_HANDLE;
    }
    m_dummyShadowReady = false;
}

// 销毁阴影采样器与渲染通道。
void VKRender3D::DestroyShadowSupport() {
    if (m_shadowRenderPass != VK_NULL_HANDLE) {
        vkDestroyRenderPass(m_device, m_shadowRenderPass, nullptr);
        m_shadowRenderPass = VK_NULL_HANDLE;
    }
    if (m_shadowSampler != VK_NULL_HANDLE) {
        vkDestroySampler(m_device, m_shadowSampler, nullptr);
        m_shadowSampler = VK_NULL_HANDLE;
    }
}

// 尝试分配指定尺寸的阴影贴图。
bool VKRender3D::TryAllocateShadowMap(uint32_t size) {
    DestroyShadowMap();
    if (!CreateShadowMap(size)) {
        UpdateShadowDescriptors();
        return false;
    }
    m_shadowMapReady = true;
    m_allocatedShadowTextureSize = size;
    m_shadowMatrixValid = false;
    UpdateShadowDescriptors();
    return true;
}

// 光照分析开启时保证阴影贴图可用。
bool VKRender3D::EnsureShadowMapForAnalysis() {
    if (!m_initialized || m_deviceLost || !m_lightAnalysisEnabled) return false;
    if (m_shadowMapReady && m_allocatedShadowTextureSize == m_shadowTextureSize) {
        return true;
    }

    const uint32_t requested = m_shadowTextureSize;
    const uint32_t previous = m_allocatedShadowTextureSize;
    const bool hadPrevious = m_shadowMapReady && previous != 0;
    WaitForIdle();

    if (TryAllocateShadowMap(requested)) {
        return true;
    }

    if (hadPrevious && TryAllocateShadowMap(previous)) {
        m_shadowTextureSize = previous;
        SetShadowMapStatus("阴影贴图创建失败，已保留 " + std::to_string(previous));
        return true;
    }

    if (requested != 2048 && previous != 2048 && TryAllocateShadowMap(2048)) {
        m_shadowTextureSize = 2048;
        SetShadowMapStatus("阴影贴图创建失败，已改用 2048");
        return true;
    }

    m_shadowTextureSize = 2048;
    m_shadowMapReady = false;
    m_allocatedShadowTextureSize = 0;
    SetShadowMapStatus("阴影贴图创建失败");
    UpdateShadowDescriptors();
    return false;
}

// 开始向阴影贴图绘制。
bool VKRender3D::BeginShadowPass() {
    if (!m_initialized || m_deviceLost || !HasUsableFramebuffer()) return false;
    if (!m_lightAnalysisEnabled || !m_sunAboveHorizon) return false;
    EnsureDummyShadowReady();
    if (!EnsureShadowMapForAnalysis() || !m_shadowMapReady || m_shadowFramebuffer == VK_NULL_HANDLE) {
        return false;
    }
    if (!EnsureFrameRecording()) return false;

    m_shadowPassActive = true;
    UpdateUniformBuffer(m_currentFrame);

    vkCmdBindDescriptorSets(m_commandBuffers[m_currentFrame], VK_PIPELINE_BIND_POINT_GRAPHICS,
        m_pipelineLayout, 0, 1, &m_shadowDescriptorSets[m_currentFrame], 0, nullptr);

    VkClearValue clearValue{};
    clearValue.depthStencil = { 1.0f, 0 };
    VkRenderPassBeginInfo renderPassInfo{};
    renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    renderPassInfo.renderPass = m_shadowRenderPass;
    renderPassInfo.framebuffer = m_shadowFramebuffer;
    renderPassInfo.renderArea.extent = { m_allocatedShadowTextureSize, m_allocatedShadowTextureSize };
    renderPassInfo.clearValueCount = 1;
    renderPassInfo.pClearValues = &clearValue;
    vkCmdBeginRenderPass(m_commandBuffers[m_currentFrame], &renderPassInfo, VK_SUBPASS_CONTENTS_INLINE);

    VkViewport viewport{};
    viewport.width = static_cast<float>(m_allocatedShadowTextureSize);
    viewport.height = static_cast<float>(m_allocatedShadowTextureSize);
    viewport.minDepth = 0.0f;
    viewport.maxDepth = 1.0f;
    vkCmdSetViewport(m_commandBuffers[m_currentFrame], 0, 1, &viewport);

    VkRect2D scissor{};
    scissor.extent = { m_allocatedShadowTextureSize, m_allocatedShadowTextureSize };
    vkCmdSetScissor(m_commandBuffers[m_currentFrame], 0, 1, &scissor);
    return true;
}

// 结束阴影通道并恢复主帧缓冲。
void VKRender3D::EndShadowPass() {
    if (!m_shadowPassActive) return;
    vkCmdEndRenderPass(m_commandBuffers[m_currentFrame]);
    m_shadowPassActive = false;
}

// 设置太阳计算用纬度。
void VKRender3D::SetLatitude(float latitude) {
    m_latitude = latitude;
    UpdateSunDirection();
}

// 设置太阳计算用日期。
void VKRender3D::SetLightDate(int year, int month, int day) {
    m_lightYear = year;
    m_lightMonth = month;
    m_lightDay = day;
    UpdateSunDirection();
}

// 设置真太阳时（分钟）。
void VKRender3D::SetLightTimeMinutes(int minutes) {
    m_lightTimeMinutes = minutes;
    UpdateSunDirection();
}

// 获取太阳光方向。
glm::vec3 VKRender3D::GetSunDirection() const {
    return m_sunDirection;
}

// 查询太阳是否位于地平线以上。
bool VKRender3D::IsSunAboveHorizon() const {
    return m_sunAboveHorizon;
}

// 按纬度/日期/真太阳时更新太阳方向。
void VKRender3D::UpdateSunDirection() {
    SolarPositionQuery query{};
    query.latitudeDegrees = m_latitude;
    query.year = m_lightYear;
    query.month = m_lightMonth;
    query.day = m_lightDay;
    query.trueSolarTimeHours = 6.0f + static_cast<float>(m_lightTimeMinutes) / 60.0f;

    const SolarPosition sun = ComputeSolarPosition(query);
    m_sunDirection = glm::vec3(sun.directionX, sun.directionY, sun.directionZ);
    m_sunAboveHorizon = sun.aboveHorizon;
}

// 开关正射投影。
void VKRender3D::SetOrthographicEnabled(bool enabled) {
    if (enabled && !m_orthographicEnabled) {
        m_modelRotation = glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
        m_mouseButton = -1;
        m_lastMouse = glm::vec2(0.0f);
        m_lastVerticalLocalAxis = glm::vec3(1.0f, 0.0f, 0.0f);
    }
    m_orthographicEnabled = enabled;
}

// 设置轨道旋转中心。
void VKRender3D::SetOrbitCenter(const Vec3& normalizedCenter) {
    m_orbitCenter = glm::vec3(
        normalizedCenter.x, normalizedCenter.y, normalizedCenter.z);
}

// 复位旋转、平移和轨道距离。
void VKRender3D::ResetView(float orbitDistance) {
    m_modelRotation = glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
    m_panOffset = glm::vec3(0.0f);
    m_orbitDistance = std::isfinite(orbitDistance)
        ? glm::clamp(orbitDistance, 0.1f, 1000.0f)
        : 3.0f;
    m_mouseButton = -1;
    m_lastMouse = glm::vec2(0.0f);
    m_lastVerticalLocalAxis = glm::vec3(1.0f, 0.0f, 0.0f);
}

// 设置归一化坐标到世界坐标的变换。
void VKRender3D::SetCoordinateNormalization(const Vec3& sourceCenter,
                                                 float normalizationScale) {
    if (!std::isfinite(normalizationScale) || normalizationScale <= 0.0f) {
        m_normalizedToWorld = glm::mat4(1.0f);
        return;
    }

    const glm::mat4 translateToSource = glm::translate(
        glm::mat4(1.0f),
        glm::vec3(sourceCenter.x, sourceCenter.y, sourceCenter.z));
    const glm::mat4 undoScale = glm::scale(
        glm::mat4(1.0f), glm::vec3(1.0f / normalizationScale));
    m_normalizedToWorld = translateToSource * undoScale;
}

// 初始化单位矩阵。
void VKRender3D::InitIdentityMatrix(float mat[4][4]) {
    memset(mat, 0, sizeof(float) * 16);
    mat[0][0] = 1.0f;
    mat[1][1] = 1.0f;
    mat[2][2] = 1.0f;
    mat[3][3] = 1.0f;
}

// 以材质参数（push constant）+ set 1（5 个纹理槽）绑定后绘制当前网格。
// 注意：不在此处绑定管线，调用方（VKMesh::DrawSubMesh）已按不透明/透明选好管线。
void VKRender3D::ApplyMaterial(const MaterialParams& params, const MaterialTextureSet& textures) {
    if (m_pipelineLayout == VK_NULL_HANDLE) return;
    VkCommandBuffer cmd = m_commandBuffers[m_currentFrame];
    // PBR 调试覆盖：按 UI 开关替换对应参数后再上传 push constant。
    MaterialParams effective = params;
    if (m_metallicOverrideEnabled) effective.metallic = m_metallicOverride;
    if (m_roughnessOverrideEnabled) effective.roughness = m_roughnessOverride;
    if (m_emissiveOverrideEnabled) effective.emissiveStrength = m_emissiveOverride;
    vkCmdPushConstants(cmd, m_pipelineLayout, VK_SHADER_STAGE_FRAGMENT_BIT,
                       0, sizeof(MaterialParams), &effective);

    const VkDescriptorSet materialSet = MaterialDescriptorSetFor(textures);
    if (materialSet != VK_NULL_HANDLE) {
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_pipelineLayout,
                                1, 1, &materialSet, 0, nullptr);
    }
}

// 计算点（上传坐标空间）到相机的距离，用于透明排序。
float VKRender3D::ComputeDrawDistance(const float center[3]) const {
    const glm::vec3 eye(0.0f, 0.0f, m_orbitDistance);
    const glm::mat4 model = glm::translate(glm::mat4(1.0f), m_panOffset)
        * glm::mat4_cast(m_modelRotation)
        * glm::translate(glm::mat4(1.0f), -m_orbitCenter);
    const glm::vec3 world = glm::vec3(model * glm::vec4(center[0], center[1], center[2], 1.0f));
    return glm::length(eye - world);
}

// 按距离从远到近排序并绘制已收集的透明请求。
void VKRender3D::FlushTransparentDraws() {
    if (m_transparentDraws.empty()) return;

    std::stable_sort(m_transparentDraws.begin(), m_transparentDraws.end(),
        [](const TransparentDraw& a, const TransparentDraw& b) {
            return a.distance > b.distance;  // 远的先画
        });

    for (const TransparentDraw& draw : m_transparentDraws) {
        if (draw.object) {
            draw.object->DrawSubMesh(draw.subMeshIndex, Object::RM_TRANSPARENT);
        }
    }
    m_transparentDraws.clear();
}

// 纹理销毁时回收引用它的材质描述符集（其余缓存保留，避免整表重建）。
void VKRender3D::OnTextureDestroyed(TextureHandle handle) {
    if (m_materialDescriptorPool == VK_NULL_HANDLE) {
        m_materialDescriptorSets.clear();
        return;
    }
    for (auto it = m_materialDescriptorSets.begin(); it != m_materialDescriptorSets.end();) {
        bool referencesHandle = false;
        for (uint32_t slot = 0; slot < kMaterialTextureSlotCount; ++slot) {
            if (it->second.handles[slot] == handle) {
                referencesHandle = true;
                break;
            }
        }
        if (referencesHandle) {
            if (it->second.set != VK_NULL_HANDLE) {
                vkFreeDescriptorSets(m_device, m_materialDescriptorPool, 1, &it->second.set);
            }
            it = m_materialDescriptorSets.erase(it);
        } else {
            ++it;
        }
    }
}


// 创建深度回读缓冲。
bool VKRender3D::CreateDepthReadbackResources() {
    VkDeviceSize bufferSize = sizeof(float);

    for (int i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
        VkBufferCreateInfo bufferInfo{};
        bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        bufferInfo.size = bufferSize;
        bufferInfo.usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT;
        bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

        if (vkCreateBuffer(m_device, &bufferInfo, nullptr, &m_depthReadbackBuffer[i]) != VK_SUCCESS) {
            return false;
        }

        VkMemoryRequirements memRequirements;
        vkGetBufferMemoryRequirements(m_device, m_depthReadbackBuffer[i], &memRequirements);

        VkMemoryAllocateInfo allocInfo{};
        allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        allocInfo.allocationSize = memRequirements.size;
        allocInfo.memoryTypeIndex = FindMemoryType(memRequirements.memoryTypeBits,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);

        if (vkAllocateMemory(m_device, &allocInfo, nullptr, &m_depthReadbackMemory[i]) != VK_SUCCESS) {
            return false;
        }

        vkBindBufferMemory(m_device, m_depthReadbackBuffer[i], m_depthReadbackMemory[i], 0);
        vkMapMemory(m_device, m_depthReadbackMemory[i], 0, bufferSize, 0, &m_depthReadbackMapped[i]);

        float initialDepth = 1.0f;
        memcpy(m_depthReadbackMapped[i], &initialDepth, sizeof(float));
    }

    return true;
}

// 销毁深度回读缓冲。
void VKRender3D::DestroyDepthReadbackResources() {
    for (int i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
        if (m_depthReadbackMapped[i] != nullptr) {
            vkUnmapMemory(m_device, m_depthReadbackMemory[i]);
            m_depthReadbackMapped[i] = nullptr;
        }
        if (m_depthReadbackBuffer[i] != VK_NULL_HANDLE) {
            vkDestroyBuffer(m_device, m_depthReadbackBuffer[i], nullptr);
            m_depthReadbackBuffer[i] = VK_NULL_HANDLE;
        }
        if (m_depthReadbackMemory[i] != VK_NULL_HANDLE) {
            vkFreeMemory(m_device, m_depthReadbackMemory[i], nullptr);
            m_depthReadbackMemory[i] = VK_NULL_HANDLE;
        }
        m_pendingReadback[i] = false;
    }
}

// 请求把屏幕点反算为世界坐标。
void VKRender3D::RequestCoordReadback(float ndcX, float ndcY) {
    m_depthReadbackRequested = true;
    m_requestedNDCX = ndcX;
    m_requestedNDCY = ndcY;
}

// 每帧结束时的钩子。
void VKRender3D::OnEndFrame() {
    if (!m_depthReadbackRequested) return;
    if (m_depthImage == VK_NULL_HANDLE) return;
    m_depthReadbackRequested = false;

    VkCommandBuffer cmd = m_commandBuffers[m_currentFrame];

    // 1.7：MSAA 开启时，深度写在多重采样图像里；先把多重采样深度解析为单采样深度。
    if (m_msaaPassActive) {
        RecordDepthResolve();
    }

    int32_t pixelX = static_cast<int32_t>(m_requestedNDCX * m_swapchainExtent.width);
    int32_t pixelY = static_cast<int32_t>(m_requestedNDCY * m_swapchainExtent.height);
    pixelX = std::max(0, std::min(pixelX, static_cast<int32_t>(m_swapchainExtent.width) - 1));
    pixelY = std::max(0, std::min(pixelY, static_cast<int32_t>(m_swapchainExtent.height) - 1));

    VkImageMemoryBarrier barrier{};
    barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barrier.oldLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
    barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = m_depthImage;
    barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
    barrier.subresourceRange.baseMipLevel = 0;
    barrier.subresourceRange.levelCount = 1;
    barrier.subresourceRange.baseArrayLayer = 0;
    barrier.subresourceRange.layerCount = 1;
    barrier.srcAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
    barrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;

    vkCmdPipelineBarrier(cmd,
        VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT,
        VK_PIPELINE_STAGE_TRANSFER_BIT,
        0, 0, nullptr, 0, nullptr, 1, &barrier);

    VkBufferImageCopy region{};
    region.bufferOffset = 0;
    region.bufferRowLength = 0;
    region.bufferImageHeight = 0;
    region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
    region.imageSubresource.mipLevel = 0;
    region.imageSubresource.baseArrayLayer = 0;
    region.imageSubresource.layerCount = 1;
    region.imageOffset = { pixelX, pixelY, 0 };
    region.imageExtent = { 1, 1, 1 };

    vkCmdCopyImageToBuffer(cmd, m_depthImage,
        VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
        m_depthReadbackBuffer[m_currentFrame], 1, &region);

    barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    barrier.newLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
    barrier.srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
    barrier.dstAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;

    vkCmdPipelineBarrier(cmd,
        VK_PIPELINE_STAGE_TRANSFER_BIT,
        VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT,
        0, 0, nullptr, 0, nullptr, 1, &barrier);

    m_pendingReadback[m_currentFrame] = true;
    m_pendingNDCX[m_currentFrame] = m_requestedNDCX;
    m_pendingNDCY[m_currentFrame] = m_requestedNDCY;
}

// 把回读深度反投影为世界坐标。
void VKRender3D::ProcessDepthReadback(uint32_t frameIndex) {
    if (!m_pendingReadback[frameIndex]) return;
    m_pendingReadback[frameIndex] = false;

    float depth;
    memcpy(&depth, m_depthReadbackMapped[frameIndex], sizeof(float));

    float x_ndc = m_pendingNDCX[frameIndex] * 2.0f - 1.0f;
    float y_ndc = 1.0f - m_pendingNDCY[frameIndex] * 2.0f;

    glm::mat4 invViewProj = m_frameInvViewProj[frameIndex];

    const glm::mat4 renderToSource = m_frameRenderToSource[frameIndex];
    glm::vec4 worldPos{};
    if (std::isfinite(depth) && depth >= 0.0f && depth < 0.999f) {
        const glm::vec4 clipPos(x_ndc, y_ndc, depth, 1.0f);
        glm::vec4 renderPos = invViewProj * clipPos;
        renderPos /= renderPos.w;
        worldPos = renderToSource * renderPos;
        worldPos /= worldPos.w;
    } else {
        glm::vec4 nearRender = invViewProj * glm::vec4(x_ndc, y_ndc, 0.0f, 1.0f);
        glm::vec4 farRender  = invViewProj * glm::vec4(x_ndc, y_ndc, 1.0f, 1.0f);
        nearRender /= nearRender.w;
        farRender  /= farRender.w;

        glm::vec4 nearWorld = renderToSource * nearRender;
        glm::vec4 farWorld = renderToSource * farRender;
        nearWorld /= nearWorld.w;
        farWorld /= farWorld.w;

        const glm::vec3 rayOrigin(nearWorld);
        const glm::vec3 rayDirection = glm::normalize(glm::vec3(farWorld - nearWorld));
        const float denominator = rayDirection.z;
        const float distance = std::abs(denominator) > 1.0e-6f
            ? -rayOrigin.z / denominator
            : 0.0f;
        worldPos = glm::vec4(rayOrigin + rayDirection * distance, 1.0f);
    }

    m_lastWorldCoord[0] = worldPos.x;
    m_lastWorldCoord[1] = worldPos.y;
    m_lastWorldCoord[2] = worldPos.z;
    m_newCoordAvailable = true;
}

// 查询是否有新的世界坐标。
bool VKRender3D::HasNewWorldCoord() const {
    bool v = m_newCoordAvailable;
    m_newCoordAvailable = false;
    return v;
}


// 获取最近一次世界坐标的 X 分量。
float VKRender3D::GetLastWorldX() const { return m_lastWorldCoord[0]; }
// 获取最近一次世界坐标的 Y 分量。
float VKRender3D::GetLastWorldY() const { return m_lastWorldCoord[1]; }
// 获取最近一次世界坐标的 Z 分量。
float VKRender3D::GetLastWorldZ() const { return m_lastWorldCoord[2]; }
