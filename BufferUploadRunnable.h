#ifndef BUFFER_UPLOAD_RUNNABLE_H
#define BUFFER_UPLOAD_RUNNABLE_H

#include <QRunnable>
#include <functional>
#include <vulkan/vulkan.h>

// 后台上传缓冲数据的 QRunnable
// 在 QThreadPool 线程中创建 staging buffer 并填充数据，
// 完成后回调主线程，由主线程创建 device-local buffer 并执行 GPU 拷贝。
class BufferUploadRunnable : public QRunnable {
public:
    using Callback = std::function<void(VkBuffer, VkDeviceMemory)>;

    BufferUploadRunnable(
        VkDevice device,
        VkPhysicalDevice physicalDevice,
        const void* data,
        VkDeviceSize size,
        VkBufferUsageFlags usage,
        Callback callback);

    ~BufferUploadRunnable() override;
    void run() override;

private:
    VkDevice m_device;
    VkPhysicalDevice m_physicalDevice;
    const void* m_data;
    VkDeviceSize m_size;
    VkBufferUsageFlags m_usage;
    Callback m_callback;
};

#endif // BUFFER_UPLOAD_RUNNABLE_H