#include "VulkanObject.h"
#include <algorithm>
#include <stdexcept>

VulkanObject::VulkanObject() { }

VulkanObject::VulkanObject(const VulkanObject& obj) {
    *this = obj;
}

VulkanObject::~VulkanObject() {
    DestroyBuffers();
}

VulkanObject& VulkanObject::operator=(const VulkanObject& obj) {
    if (this != &obj) {
        m_pParent = obj.m_pParent;
        m_pRender = obj.m_pRender;
        m_uType = obj.m_uType;
        m_uFlag = obj.m_uFlag;
        m_uClr = obj.m_uClr;
    }
    return *this;
}

// ============================================================================
// 渲染器关联
// ============================================================================

void VulkanObject::SetRender(VulkanRender* pRender) {
    m_pRender = pRender;
}

VulkanRender* VulkanObject::GetRender() const {
    return m_pRender;
}

// ============================================================================
// 父对象管理
// ============================================================================

void VulkanObject::SetParent(VulkanObject* pParent) {
    m_pParent = pParent;
}

VulkanObject* VulkanObject::GetParent() const {
    return m_pParent;
}

void VulkanObject::SetDirty(bool bDirty) {
    EnableFlag(FT_DIRTY, bDirty);
}

bool VulkanObject::IsDirty() const {
    return IsFlagEnabled(FT_DIRTY);
}

// ============================================================================
// 对象类型
// ============================================================================

uint32_t VulkanObject::GetType() const {
    return m_uType;
}

// ============================================================================
// 可见性控制
// ============================================================================

void VulkanObject::SetVisible(bool bVisible) {
    EnableFlag(FT_VISIBLE, bVisible);
}

bool VulkanObject::IsVisible() const {
    return IsFlagEnabled(FT_VISIBLE);
}

// ============================================================================
// 颜色管理
// ============================================================================

void VulkanObject::SetColor(const Vec4& clr) {
    uint8_t r = static_cast<uint8_t>(clr.x * 255.0f);
    uint8_t g = static_cast<uint8_t>(clr.y * 255.0f);
    uint8_t b = static_cast<uint8_t>(clr.z * 255.0f);
    uint8_t a = static_cast<uint8_t>(clr.w * 255.0f);
    m_uClr = (static_cast<uint32_t>(r)) |
             (static_cast<uint32_t>(g) << 8) |
             (static_cast<uint32_t>(b) << 16) |
             (static_cast<uint32_t>(a) << 24);
}

void VulkanObject::SetColor(float r, float g, float b, float a) {
    SetColor(Vec4{ r, g, b, a });
}

Vec4 VulkanObject::GetColor() const {
    float r = static_cast<float>((m_uClr >> 0) & 0xFF) / 255.0f;
    float g = static_cast<float>((m_uClr >> 8) & 0xFF) / 255.0f;
    float b = static_cast<float>((m_uClr >> 16) & 0xFF) / 255.0f;
    float a = static_cast<float>((m_uClr >> 24) & 0xFF) / 255.0f;
    return Vec4{ r, g, b, a };
}

// ============================================================================
// 标志位操作
// ============================================================================

void VulkanObject::EnableFlag(FlagType ft, bool bEnable) {
    if (bEnable) {
        m_uFlag |= static_cast<uint8_t>(ft);
    } else {
        m_uFlag &= ~static_cast<uint8_t>(ft);
    }
}

bool VulkanObject::IsFlagEnabled(FlagType ft) const {
    return (m_uFlag & static_cast<uint8_t>(ft)) != 0;
}

// ============================================================================
// Vulkan缓冲区管理
// ============================================================================

// 创建顶点缓冲区
void VulkanObject::CreateVertexBuffer(const std::vector<VulkanVertex>& vertices) {
    if (!m_pRender || vertices.empty()) return;

    // 销毁旧缓冲区
    DestroyBuffers();

    VkDeviceSize bufferSize = sizeof(VulkanVertex) * vertices.size();

    // 创建暂存缓冲区（CPU可读写）
    VkBuffer stagingBuffer;
    VkDeviceMemory stagingBufferMemory;
    CreateBufferHelper(bufferSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                       VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                       stagingBuffer, stagingBufferMemory);

    // 映射并复制数据
    void* data;
    vkMapMemory(m_pRender->GetDevice(), stagingBufferMemory, 0, bufferSize, 0, &data);
    memcpy(data, vertices.data(), static_cast<size_t>(bufferSize));
    vkUnmapMemory(m_pRender->GetDevice(), stagingBufferMemory);

    // 创建设备本地缓冲区（GPU专用）
    CreateBufferHelper(bufferSize, VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
                       VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
                       m_vertexBuffer, m_vertexBufferMemory);

    // 从暂存缓冲区复制到设备本地缓冲区
    CopyBuffer(stagingBuffer, m_vertexBuffer, bufferSize);

    // 销毁暂存缓冲区
    vkDestroyBuffer(m_pRender->GetDevice(), stagingBuffer, nullptr);
    vkFreeMemory(m_pRender->GetDevice(), stagingBufferMemory, nullptr);
}

// 创建索引缓冲区
void VulkanObject::CreateIndexBuffer(const std::vector<uint32_t>& indices) {
    if (!m_pRender || indices.empty()) return;

    m_indexCount = static_cast<uint32_t>(indices.size());
    VkDeviceSize bufferSize = sizeof(uint32_t) * indices.size();

    // 创建暂存缓冲区
    VkBuffer stagingBuffer;
    VkDeviceMemory stagingBufferMemory;
    CreateBufferHelper(bufferSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                       VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                       stagingBuffer, stagingBufferMemory);

    // 映射并复制数据
    void* data;
    vkMapMemory(m_pRender->GetDevice(), stagingBufferMemory, 0, bufferSize, 0, &data);
    memcpy(data, indices.data(), static_cast<size_t>(bufferSize));
    vkUnmapMemory(m_pRender->GetDevice(), stagingBufferMemory);

    // 创建设备本地缓冲区
    CreateBufferHelper(bufferSize, VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
                       VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
                       m_indexBuffer, m_indexBufferMemory);

    // 复制数据
    CopyBuffer(stagingBuffer, m_indexBuffer, bufferSize);

    // 销毁暂存缓冲区
    vkDestroyBuffer(m_pRender->GetDevice(), stagingBuffer, nullptr);
    vkFreeMemory(m_pRender->GetDevice(), stagingBufferMemory, nullptr);
}

// 更新顶点缓冲区
void VulkanObject::UpdateVertexBuffer(const std::vector<VulkanVertex>& vertices) {
    if (!m_pRender || vertices.empty()) return;

    VkDeviceSize bufferSize = sizeof(VulkanVertex) * vertices.size();

    void* data;
    vkMapMemory(m_pRender->GetDevice(), m_vertexBufferMemory, 0, bufferSize, 0, &data);
    memcpy(data, vertices.data(), static_cast<size_t>(bufferSize));
    vkUnmapMemory(m_pRender->GetDevice(), m_vertexBufferMemory);
}

// 更新索引缓冲区
void VulkanObject::UpdateIndexBuffer(const std::vector<uint32_t>& indices) {
    if (!m_pRender || indices.empty()) return;

    m_indexCount = static_cast<uint32_t>(indices.size());
    VkDeviceSize bufferSize = sizeof(uint32_t) * indices.size();

    void* data;
    vkMapMemory(m_pRender->GetDevice(), m_indexBufferMemory, 0, bufferSize, 0, &data);
    memcpy(data, indices.data(), static_cast<size_t>(bufferSize));
    vkUnmapMemory(m_pRender->GetDevice(), m_indexBufferMemory);
}

// 销毁缓冲区
void VulkanObject::DestroyBuffers() {
    if (!m_pRender) return;

    VkDevice device = m_pRender->GetDevice();

    if (m_indexBuffer != VK_NULL_HANDLE) {
        vkDestroyBuffer(device, m_indexBuffer, nullptr);
        m_indexBuffer = VK_NULL_HANDLE;
    }
    if (m_indexBufferMemory != VK_NULL_HANDLE) {
        vkFreeMemory(device, m_indexBufferMemory, nullptr);
        m_indexBufferMemory = VK_NULL_HANDLE;
    }

    if (m_vertexBuffer != VK_NULL_HANDLE) {
        vkDestroyBuffer(device, m_vertexBuffer, nullptr);
        m_vertexBuffer = VK_NULL_HANDLE;
    }
    if (m_vertexBufferMemory != VK_NULL_HANDLE) {
        vkFreeMemory(device, m_vertexBufferMemory, nullptr);
        m_vertexBufferMemory = VK_NULL_HANDLE;
    }
}

// ============================================================================
// 辅助函数
// ============================================================================

// 创建缓冲区（内部辅助）
void VulkanObject::CreateBufferHelper(VkDeviceSize size, VkBufferUsageFlags usage,
                                      VkMemoryPropertyFlags properties,
                                      VkBuffer& buffer, VkDeviceMemory& bufferMemory) {
    VkBufferCreateInfo bufferInfo{};
    bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufferInfo.size = size;
    bufferInfo.usage = usage;
    bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    if (vkCreateBuffer(m_pRender->GetDevice(), &bufferInfo, nullptr, &buffer) != VK_SUCCESS) {
        throw std::runtime_error("创建缓冲区失败");
    }

    VkMemoryRequirements memRequirements;
    vkGetBufferMemoryRequirements(m_pRender->GetDevice(), buffer, &memRequirements);

    VkMemoryAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocInfo.allocationSize = memRequirements.size;
    allocInfo.memoryTypeIndex = FindMemoryType(memRequirements.memoryTypeBits, properties);

    if (vkAllocateMemory(m_pRender->GetDevice(), &allocInfo, nullptr, &bufferMemory) != VK_SUCCESS) {
        throw std::runtime_error("分配缓冲区内存失败");
    }

    vkBindBufferMemory(m_pRender->GetDevice(), buffer, bufferMemory, 0);
}

// 查找内存类型
uint32_t VulkanObject::FindMemoryType(uint32_t typeFilter, VkMemoryPropertyFlags properties) {
    VkPhysicalDeviceMemoryProperties memProperties;
    vkGetPhysicalDeviceMemoryProperties(m_pRender->GetPhysicalDevice(), &memProperties);

    for (uint32_t i = 0; i < memProperties.memoryTypeCount; i++) {
        if ((typeFilter & (1 << i)) && (memProperties.memoryTypes[i].propertyFlags & properties) == properties) {
            return i;
        }
    }

    throw std::runtime_error("未找到合适的内存类型");
}

// 复制缓冲区
void VulkanObject::CopyBuffer(VkBuffer srcBuffer, VkBuffer dstBuffer, VkDeviceSize size) {
    VkCommandBuffer commandBuffer = m_pRender->BeginSingleTimeCommands();

    VkBufferCopy copyRegion{};
    copyRegion.size = size;
    vkCmdCopyBuffer(commandBuffer, srcBuffer, dstBuffer, 1, &copyRegion);

    m_pRender->EndSingleTimeCommands(commandBuffer);
}

VkBuffer VulkanObject::GetVertexBuffer() const { return m_vertexBuffer; }

VkBuffer VulkanObject::GetIndexBuffer() const { return m_indexBuffer; }

uint32_t VulkanObject::GetIndexCount() const { return m_indexCount; }