#ifndef BUFFER_UPLOAD_RUNNABLE_H
#define BUFFER_UPLOAD_RUNNABLE_H

#include <QRunnable>
#include <functional>
#include <vector>
#include <vulkan/vulkan.h>

class BufferUploadRunnable : public QRunnable {
public:
    using Callback = std::function<void(VkBuffer, VkDeviceMemory)>;

    struct UploadSegment {
        const void* data = nullptr;
        VkDeviceSize size = 0;
        VkBufferUsageFlags usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
        VkDeviceSize stagingOffset = 0; // run() 内填充
    };

    using MultiCallback = std::function<void(VkBuffer, VkDeviceMemory,
                                              const std::vector<UploadSegment>&)>;

    BufferUploadRunnable(
        VkDevice device,
        VkPhysicalDevice physicalDevice,
        const void* data,
        VkDeviceSize size,
        VkBufferUsageFlags usage,
        Callback callback);

    BufferUploadRunnable(
        VkDevice device,
        VkPhysicalDevice physicalDevice,
        std::vector<UploadSegment> segments,
        MultiCallback callback);

    ~BufferUploadRunnable() override;
    void run() override;

private:
    VkDevice m_device;
    VkPhysicalDevice m_physicalDevice;

    const void* m_data = nullptr;
    VkDeviceSize m_size = 0;
    VkBufferUsageFlags m_usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;

    std::vector<UploadSegment> m_segments;

    Callback m_callback;
    MultiCallback m_multiCallback;

    bool m_isMulti = false;
};

#endif // BUFFER_UPLOAD_RUNNABLE_H
