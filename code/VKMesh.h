#pragma once

#include "Object/VKObject.h"
#include "Asset/MeshData.h"
#include "VertexType/VertexTypes.h"
#include "BufferUploadRunnable.h"
#include <memory>
#include <atomic>

class VKMesh : public VKObject {
public:
    VKMesh();
    ~VKMesh() override;

    // 从资产网格上传（内部拍平为 Vertex3D）。
    void SetMeshData(const MeshData& mesh);

    // 启动 Mesh 数据的异步上传。
    void SetMeshData(std::vector<Vertex3D>&& vertices, std::vector<uint32_t>&& indices);

    // 同步上传 Mesh 数据。
    void SetMeshDataSync(const std::vector<Vertex3D>& vertices, const std::vector<uint32_t>& indices);

    // 绑定管线与缓冲并按当前模式绘制。
    void Render(int mode = 0) override;

private:
    // 顶点与索引缓冲都就绪后置位。
    void CheckBuffersReady();

    // staging 完成后创建设备缓冲。
    void OnCombinedBuffersUploaded(VkBuffer staging, VkDeviceMemory stagingMem, const std::vector<BufferUploadRunnable::UploadSegment>& segments);

    // 销毁 staging 缓冲与内存。
    void CleanupStaging(VkBuffer staging, VkDeviceMemory stagingMem);

    std::shared_ptr<std::atomic<bool>> m_alive;
    bool m_buffersReady = false;

    uint64_t m_uploadGeneration = 0;
};

