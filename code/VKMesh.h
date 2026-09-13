#pragma once

#include "Object/VKObject.h"
#include "VertexType/VertexTypes.h"
#include "BufferUploadRunnable.h"
#include <memory>
#include <atomic>

class VKMesh : public VKObject {
public:
    VKMesh();
    ~VKMesh() override;

    // 启动 Mesh 数据的异步上传。
    void SetMeshData(std::vector<Vertex3D>&& vertices,
                     std::vector<uint32_t>&& indices);

    // 同步上传 Mesh 数据。
    void SetMeshDataSync(const std::vector<Vertex3D>& vertices,
                         const std::vector<uint32_t>& indices);

    // 绘制自身；Layer 则遍历子对象。
    void Render(int mode = 0) override;

private:
    // 检查 Mesh GPU 缓冲区是否就绪。
    void CheckBuffersReady();

    // 处理合并缓冲区上传完成回调。
    void OnCombinedBuffersUploaded(VkBuffer staging, VkDeviceMemory stagingMem,
                                   const std::vector<BufferUploadRunnable::UploadSegment>& segments);

    // 释放 staging 缓冲区资源。
    void CleanupStaging(VkBuffer staging, VkDeviceMemory stagingMem);

    std::shared_ptr<std::atomic<bool>> m_alive;
    bool m_buffersReady = false;

    uint64_t m_uploadGeneration = 0;
};

