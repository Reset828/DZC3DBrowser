#ifndef BUFFER_UPLOAD_RUNNABLE_H
#define BUFFER_UPLOAD_RUNNABLE_H

#include <QRunnable>
#include <functional>
#include <vector>
#include <vulkan/vulkan.h>

// 后台上传缓冲数据的 QRunnable
// 在 QThreadPool 线程中创建 staging buffer 并填充数据，
// 完成后回调主线程，由主线程创建 device-local buffer 并执行 GPU 拷贝。
class BufferUploadRunnable : public QRunnable {
public:
    // 单段上传的回调
    using Callback = std::function<void(VkBuffer, VkDeviceMemory)>;

    // 上传段描述（用于多段合并上传）
    struct UploadSegment {
        const void* data = nullptr;
        VkDeviceSize size = 0;
        VkBufferUsageFlags usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
        VkDeviceSize stagingOffset = 0; // run() 内填充
    };

    // 多段上传的回调（接收填充后的段列表以获取 staging 内偏移）
    using MultiCallback = std::function<void(VkBuffer, VkDeviceMemory,
                                              const std::vector<UploadSegment>&)>;

    // 单段上传（兼容原接口）
    BufferUploadRunnable(
        VkDevice device,
        VkPhysicalDevice physicalDevice,
        const void* data,
        VkDeviceSize size,
        VkBufferUsageFlags usage,
        Callback callback);

    // 多段合并上传：同一 staging buffer 容纳多个数据段，减少资源创建
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

    // 单段参数
    const void* m_data = nullptr;
    VkDeviceSize m_size = 0;
    VkBufferUsageFlags m_usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;

    // 多段参数
    std::vector<UploadSegment> m_segments;

    Callback m_callback;
    MultiCallback m_multiCallback;

    bool m_isMulti = false;
};

#endif // BUFFER_UPLOAD_RUNNABLE_H