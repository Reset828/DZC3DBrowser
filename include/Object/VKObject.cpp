#include "VKObject.h"
#include "Render/VKRender.h"
#include <cstring>

VKObject::VKObject() {}

VKObject::VKObject(const VKObject& obj) {
    *this = obj;
}

VKObject::~VKObject() {
    DestroyBuffers();
}

VKObject& VKObject::operator=(const VKObject& obj) {
    if (this != &obj) {
        Object::operator=(obj);
        m_pRender = obj.m_pRender;
    }
    return *this;
}

// 绑定所属渲染器。
void VKObject::SetRender(VKRender* pRender) {
    m_pRender = pRender;
}

// 获取 Vulkan 渲染器。
VKRender* VKObject::GetRender() const {
    return m_pRender;
}

// 创建顶点缓冲区。
void VKObject::CreateVertexBuffer(const void* data, VkDeviceSize size) {
    if (!m_pRender || !data || size == 0) return;
    DestroyBuffers();
    CreateBufferFromData(data, size, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, m_vertexBuffer);
}

// 创建索引缓冲区。
void VKObject::CreateIndexBuffer(const void* data, VkDeviceSize size,
                                 uint32_t indexCount) {
    if (!m_pRender || !data || size == 0 || indexCount == 0) return;
    CreateBufferFromData(data, size, VK_BUFFER_USAGE_INDEX_BUFFER_BIT, m_indexBuffer);
    m_indexCount = indexCount;
}

// 更新已有顶点缓冲区内容。
void VKObject::UpdateVertexBuffer(const void* data, VkDeviceSize size) {
    UpdateBufferFromData(data, size, m_vertexBuffer);
}

// 更新已有索引缓冲区内容。
void VKObject::UpdateIndexBuffer(const void* data, VkDeviceSize size,
                                 uint32_t indexCount) {
    UpdateBufferFromData(data, size, m_indexBuffer);
    if (data && size > 0) {
        m_indexCount = indexCount;
    }
}

// 销毁对象持有的 GPU 缓冲区。
void VKObject::DestroyBuffers() {
    if (!m_pRender) return;

    if (m_indexBuffer != VK_NULL_HANDLE) {
        m_pRender->DestroyBuffer(m_indexBuffer);
        m_indexBuffer = VK_NULL_HANDLE;
    }
    if (m_vertexBuffer != VK_NULL_HANDLE) {
        m_pRender->DestroyBuffer(m_vertexBuffer);
        m_vertexBuffer = VK_NULL_HANDLE;
    }
    m_indexCount = 0;
}

// 经 staging 创建设备本地缓冲区。
void VKObject::CreateBufferFromData(const void* data, VkDeviceSize size,
                                    VkBufferUsageFlags usage, VkBuffer& target) {
    if (!m_pRender || !data || size == 0) return;

    if (target != VK_NULL_HANDLE) {
        m_pRender->DestroyBuffer(target);
        target = VK_NULL_HANDLE;
    }

    VkBuffer stagingBuffer = VK_NULL_HANDLE;
    VkBuffer deviceBuffer = VK_NULL_HANDLE;
    try {
        stagingBuffer = m_pRender->CreateBuffer(
            size, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);

        void* mapped = nullptr;
        if (m_pRender->MapBuffer(stagingBuffer, &mapped) != VK_SUCCESS) {
            m_pRender->DestroyBuffer(stagingBuffer);
            return;
        }
        std::memcpy(mapped, data, static_cast<size_t>(size));
        m_pRender->UnmapBuffer(stagingBuffer);

        deviceBuffer = m_pRender->CreateBuffer(
            size, VK_BUFFER_USAGE_TRANSFER_DST_BIT | usage,
            VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
        m_pRender->CopyBuffer(stagingBuffer, deviceBuffer, size);
        m_pRender->DestroyBuffer(stagingBuffer);
        target = deviceBuffer;
    } catch (...) {
        if (deviceBuffer != VK_NULL_HANDLE) {
            m_pRender->DestroyBuffer(deviceBuffer);
        }
        if (stagingBuffer != VK_NULL_HANDLE) {
            m_pRender->DestroyBuffer(stagingBuffer);
        }
        throw;
    }
}

// 把数据写入已有缓冲区。
void VKObject::UpdateBufferFromData(const void* data, VkDeviceSize size,
                                    VkBuffer target) {
    if (!m_pRender || !data || size == 0 || target == VK_NULL_HANDLE) return;

    void* mapped = nullptr;
    if (m_pRender->MapBuffer(target, &mapped) != VK_SUCCESS) return;
    std::memcpy(mapped, data, static_cast<size_t>(size));
    m_pRender->UnmapBuffer(target);
}

// 获取顶点缓冲区。
VkBuffer VKObject::GetVertexBuffer() const { return m_vertexBuffer; }

// 获取索引缓冲区。
VkBuffer VKObject::GetIndexBuffer() const { return m_indexBuffer; }

// 获取索引数量。
uint32_t VKObject::GetIndexCount() const { return m_indexCount; }
