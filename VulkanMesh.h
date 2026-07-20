#pragma once

#include "VulkanObject.h"
#include "VertexTypes.h"
#include "BufferUploadRunnable.h"
#include <memory>
#include <atomic>

class VulkanMesh : public VulkanObject {
public:
    VulkanMesh();
    ~VulkanMesh() override;

    void SetMeshData(std::vector<Vertex3D>&& vertices,
                     std::vector<uint32_t>&& indices);

    void SetMeshDataSync(const std::vector<Vertex3D>& vertices,
                         const std::vector<uint32_t>& indices);

    void Render(int mode = 0) override;

private:
    void OnVertexBufferUploaded(VkBuffer staging, VkDeviceMemory stagingMem,
                                VkDeviceSize bufferSize);
    void OnIndexBufferUploaded(VkBuffer staging, VkDeviceMemory stagingMem,
                               VkDeviceSize bufferSize);

    void CheckBuffersReady();

    void OnCombinedBuffersUploaded(VkBuffer staging, VkDeviceMemory stagingMem,
                                   const std::vector<BufferUploadRunnable::UploadSegment>& segments);

    void CleanupStaging(VkBuffer staging, VkDeviceMemory stagingMem);

    std::shared_ptr<std::atomic<bool>> m_alive;
    bool m_buffersReady = false;

    uint64_t m_uploadGeneration = 0;
};


