#include "BufferUploadRunnable.h"
#include <QApplication>
#include <QMetaObject>
#include <cstring>
#include <stdexcept>

BufferUploadRunnable::BufferUploadRunnable(
    VkDevice device,
    VkPhysicalDevice physicalDevice,
    const void* data,
    VkDeviceSize size,
    VkBufferUsageFlags usage,
    Callback callback)
    : m_device(device)
    , m_physicalDevice(physicalDevice)
    , m_data(data)
    , m_size(size)
    , m_usage(usage)
    , m_callback(std::move(callback))
    , m_isMulti(false)
{
    setAutoDelete(true);
}

BufferUploadRunnable::BufferUploadRunnable(
    VkDevice device,
    VkPhysicalDevice physicalDevice,
    std::vector<UploadSegment> segments,
    MultiCallback callback)
    : m_device(device)
    , m_physicalDevice(physicalDevice)
    , m_segments(std::move(segments))
    , m_multiCallback(std::move(callback))
    , m_isMulti(true)
{
    setAutoDelete(true);
}

BufferUploadRunnable::~BufferUploadRunnable() = default;

void BufferUploadRunnable::run() {
    // --- 0. 计算总大小（多段） ---
    VkDeviceSize totalSize = m_size;
    if (m_isMulti) {
        totalSize = 0;
        VkDeviceSize offset = 0;
        for (auto& seg : m_segments) {
            seg.stagingOffset = offset;
            totalSize += seg.size;
            offset += seg.size;
        }
    }

    // --- 1. 创建 staging buffer ---
    VkBufferCreateInfo bufferInfo{};
    bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufferInfo.size = totalSize;
    bufferInfo.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
    bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    VkBuffer stagingBuffer = VK_NULL_HANDLE;
    if (vkCreateBuffer(m_device, &bufferInfo, nullptr, &stagingBuffer) != VK_SUCCESS) {
        throw std::runtime_error("BufferUploadRunnable: 创建 staging buffer 失败");
    }

    // --- 2. 获取内存要求 ---
    VkMemoryRequirements memReq;
    vkGetBufferMemoryRequirements(m_device, stagingBuffer, &memReq);

    // --- 3. 查找 HOST_VISIBLE | HOST_COHERENT 内存类型 ---
    VkPhysicalDeviceMemoryProperties memProps;
    vkGetPhysicalDeviceMemoryProperties(m_physicalDevice, &memProps);

    uint32_t memTypeIndex = UINT32_MAX;
    for (uint32_t i = 0; i < memProps.memoryTypeCount; i++) {
        if ((memReq.memoryTypeBits & (1 << i)) &&
            (memProps.memoryTypes[i].propertyFlags & (VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                                                      VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)) ==
                (VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)) {
            memTypeIndex = i;
            break;
        }
    }

    if (memTypeIndex == UINT32_MAX) {
        vkDestroyBuffer(m_device, stagingBuffer, nullptr);
        throw std::runtime_error("BufferUploadRunnable: 未找到可用内存类型");
    }

    // --- 4. 分配并绑定 staging 内存 ---
    VkMemoryAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocInfo.allocationSize = memReq.size;
    allocInfo.memoryTypeIndex = memTypeIndex;

    VkDeviceMemory stagingMemory = VK_NULL_HANDLE;
    if (vkAllocateMemory(m_device, &allocInfo, nullptr, &stagingMemory) != VK_SUCCESS) {
        vkDestroyBuffer(m_device, stagingBuffer, nullptr);
        throw std::runtime_error("BufferUploadRunnable: 分配 staging 内存失败");
    }

    vkBindBufferMemory(m_device, stagingBuffer, stagingMemory, 0);

    // --- 5. 映射并拷贝数据 ---
    void* mapped = nullptr;
    vkMapMemory(m_device, stagingMemory, 0, totalSize, 0, &mapped);

    if (m_isMulti) {
        for (const auto& seg : m_segments) {
            std::memcpy(static_cast<char*>(mapped) + seg.stagingOffset,
                        seg.data, static_cast<size_t>(seg.size));
        }
    } else {
        std::memcpy(mapped, m_data, static_cast<size_t>(m_size));
    }

    vkUnmapMemory(m_device, stagingMemory);

    // --- 6. 回调主线程处理 device-local 创建和 GPU 拷贝 ---
    auto staging = stagingBuffer;
    auto memory = stagingMemory;

    if (m_isMulti) {
        auto segments = m_segments;
        auto callback = m_multiCallback;
        QMetaObject::invokeMethod(QApplication::instance(),
            [staging, memory, segments, callback]() {
                callback(staging, memory, segments);
            }, Qt::QueuedConnection);
    } else {
        auto callback = m_callback;
        QMetaObject::invokeMethod(QApplication::instance(), [staging, memory, callback]() {
            callback(staging, memory);
        }, Qt::QueuedConnection);
    }
}
