#include "VulkanObject.h"
#include <algorithm>
#include <stdexcept>
#include <cstring>

VulkanObject::VulkanObject() {}

VulkanObject::VulkanObject(const VulkanObject& obj) {
    *this = obj;
}

VulkanObject::~VulkanObject() {
    DestroyBuffers();
}

VulkanObject& VulkanObject::operator=(const VulkanObject& obj) {
    if (this != &obj) {
        SceneObject::operator=(obj);
        m_pRender = obj.m_pRender;
    }
    return *this;
}

void VulkanObject::SetRender(VKRender* pRender) {
    m_pRender = pRender;
}

VKRender* VulkanObject::GetRender() const {
    return m_pRender;
}

void VulkanObject::CreateVertexBuffer(const std::vector<Vertex3D>& vertices) {
    if (!m_pRender || vertices.empty()) return;

    DestroyBuffers();

    VkDeviceSize bufferSize = sizeof(Vertex3D) * vertices.size();

    VkBuffer stagingBuffer;
    VkDeviceMemory stagingBufferMemory;
    CreateBufferHelper(bufferSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                       VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                       stagingBuffer, stagingBufferMemory);

    void* data;
    vkMapMemory(m_pRender->GetDevice(), stagingBufferMemory, 0, bufferSize, 0, &data);
    memcpy(data, vertices.data(), static_cast<size_t>(bufferSize));
    vkUnmapMemory(m_pRender->GetDevice(), stagingBufferMemory);

    CreateBufferHelper(bufferSize, VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
                       VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
                       m_vertexBuffer, m_vertexBufferMemory);

    CopyBuffer(stagingBuffer, m_vertexBuffer, bufferSize);

    vkDestroyBuffer(m_pRender->GetDevice(), stagingBuffer, nullptr);
    vkFreeMemory(m_pRender->GetDevice(), stagingBufferMemory, nullptr);
}

void VulkanObject::CreateIndexBuffer(const std::vector<uint32_t>& indices) {
    if (!m_pRender || indices.empty()) return;

    m_indexCount = static_cast<uint32_t>(indices.size());
    VkDeviceSize bufferSize = sizeof(uint32_t) * indices.size();

    VkBuffer stagingBuffer;
    VkDeviceMemory stagingBufferMemory;
    CreateBufferHelper(bufferSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                       VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                       stagingBuffer, stagingBufferMemory);

    void* data;
    vkMapMemory(m_pRender->GetDevice(), stagingBufferMemory, 0, bufferSize, 0, &data);
    memcpy(data, indices.data(), static_cast<size_t>(bufferSize));
    vkUnmapMemory(m_pRender->GetDevice(), stagingBufferMemory);

    CreateBufferHelper(bufferSize, VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
                       VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
                       m_indexBuffer, m_indexBufferMemory);

    CopyBuffer(stagingBuffer, m_indexBuffer, bufferSize);

    vkDestroyBuffer(m_pRender->GetDevice(), stagingBuffer, nullptr);
    vkFreeMemory(m_pRender->GetDevice(), stagingBufferMemory, nullptr);
}

void VulkanObject::UpdateVertexBuffer(const std::vector<Vertex3D>& vertices) {
    if (!m_pRender || vertices.empty()) return;

    VkDeviceSize bufferSize = sizeof(Vertex3D) * vertices.size();

    void* data;
    vkMapMemory(m_pRender->GetDevice(), m_vertexBufferMemory, 0, bufferSize, 0, &data);
    memcpy(data, vertices.data(), static_cast<size_t>(bufferSize));
    vkUnmapMemory(m_pRender->GetDevice(), m_vertexBufferMemory);
}

void VulkanObject::UpdateIndexBuffer(const std::vector<uint32_t>& indices) {
    if (!m_pRender || indices.empty()) return;

    m_indexCount = static_cast<uint32_t>(indices.size());
    VkDeviceSize bufferSize = sizeof(uint32_t) * indices.size();

    void* data;
    vkMapMemory(m_pRender->GetDevice(), m_indexBufferMemory, 0, bufferSize, 0, &data);
    memcpy(data, indices.data(), static_cast<size_t>(bufferSize));
    vkUnmapMemory(m_pRender->GetDevice(), m_indexBufferMemory);
}

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
