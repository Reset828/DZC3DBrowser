#pragma once

#include "VulkanObject.h"
#include "VertexTypes.h"
#include <memory>
#include <atomic>

// 网格类 —— 持有 OBJ 解析后的几何数据并负责渲染
// 数据通过异步方式上传到 GPU，不会阻塞主线程。
class VulkanMesh : public VulkanObject {
public:
    VulkanMesh();
    ~VulkanMesh() override;

    // 设置网格数据（从 OBJ 解析结果），隐式启动异步 GPU 上传
    void SetMeshData(std::vector<Vertex3D>&& vertices,
                     std::vector<uint32_t>&& indices);

    // 创建同步方式（用于小数据量的紧急上传）
    void SetMeshDataSync(const std::vector<Vertex3D>& vertices,
                         const std::vector<uint32_t>& indices);

    void Render(int mode = 0) override;

private:
    // 异步上传完成回调（在主线程执行）
    void OnVertexBufferUploaded(VkBuffer staging, VkDeviceMemory stagingMem,
                                VkDeviceSize bufferSize);
    void OnIndexBufferUploaded(VkBuffer staging, VkDeviceMemory stagingMem,
                               VkDeviceSize bufferSize);

    // 检查 vertex + index 是否都已就绪
    void CheckBuffersReady();

    // 清理 staging buffer（对象已销毁时使用）
    void CleanupStaging(VkBuffer staging, VkDeviceMemory stagingMem);

    // 生命周期令牌：对象销毁时标记失效，阻止异步回调访问野指针
    std::shared_ptr<std::atomic<bool>> m_alive;
    bool m_buffersReady = false;
};


