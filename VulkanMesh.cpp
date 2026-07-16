#include "VulkanMesh.h"
#include "BufferUploadRunnable.h"
#include <QApplication>
#include <QMetaObject>

VulkanMesh::VulkanMesh()
    : m_alive(std::make_shared<std::atomic<bool>>(true))
{
    m_uType = OT_OBJECT;
}

VulkanMesh::~VulkanMesh() {
    *m_alive = false; // 标记已销毁，阻止未执行的异步回调继续
}

// ============================================================================
// 公有接口
// ============================================================================

void VulkanMesh::SetMeshData(std::vector<Vertex3D>&& vertices,
                              std::vector<uint32_t>&& indices) {
    if (!m_pRender || vertices.empty() || indices.empty()) return;

    // 清理旧缓冲
    DestroyBuffers();
    m_buffersReady = false;

    VkDevice device         = m_pRender->GetDevice();
    VkPhysicalDevice phyDev = m_pRender->GetPhysicalDevice();

    VkDeviceSize vertSize = vertices.size() * sizeof(Vertex3D);
    VkDeviceSize idxSize  = indices.size()  * sizeof(uint32_t);
    m_indexCount = static_cast<uint32_t>(indices.size());

    // 保持原始数据在 QRunnable 执行期间存活
    auto vertData = std::make_shared<std::vector<Vertex3D>>(std::move(vertices));
    auto idxData  = std::make_shared<std::vector<uint32_t>>(std::move(indices));

    // 生命周期令牌（值拷贝，使 lambda 可拷贝）
    auto alive = m_alive;

    // 递增上传代号，旧回调将因代号不匹配而跳过
    ++m_uploadGeneration;
    uint64_t gen = m_uploadGeneration;

    // 合并上传：同一 staging buffer 容纳顶点和索引数据
    std::vector<BufferUploadRunnable::UploadSegment> segments;
    segments.push_back({ vertData->data(), vertSize, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT });
    segments.push_back({ idxData->data(),  idxSize,  VK_BUFFER_USAGE_INDEX_BUFFER_BIT });

    auto segsCopy = std::make_shared<std::vector<BufferUploadRunnable::UploadSegment>>(segments);

    auto task = new BufferUploadRunnable(device, phyDev, std::move(segments),
        [alive, this, vertData, idxData, segsCopy, gen](VkBuffer staging,
                                                         VkDeviceMemory stagingMem,
            const std::vector<BufferUploadRunnable::UploadSegment>& resultSegs) {
            if (!*alive || gen != m_uploadGeneration) {
                VkDevice dev = m_pRender ? m_pRender->GetDevice() : VK_NULL_HANDLE;
                if (dev != VK_NULL_HANDLE) {
                    vkDestroyBuffer(dev, staging, nullptr);
                    vkFreeMemory(dev, stagingMem, nullptr);
                }
                return;
            }
            OnCombinedBuffersUploaded(staging, stagingMem, resultSegs);
        });

    m_pRender->SubmitAsync(task);
}

void VulkanMesh::SetMeshDataSync(const std::vector<Vertex3D>& vertices,
                                  const std::vector<uint32_t>& indices) {
    if (!m_pRender || vertices.empty() || indices.empty()) return;

    DestroyBuffers();
    m_buffersReady = false;

    CreateVertexBuffer(vertices);
    CreateIndexBuffer(indices);
    m_buffersReady = true;
}

void VulkanMesh::Render(int /*mode*/) {
    if (!IsVisible() || !m_buffersReady || !m_pRender) return;
    if (m_indexCount == 0) return;

    VkCommandBuffer cmd = m_pRender->GetCurrentCommandBuffer();

    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                      m_pRender->GetPipeline(m_pRender->IsWireframeEnabled()
                          ? VulkanRender::DT_TRIANGLE_WIREFRAME
                          : VulkanRender::DT_TRIANGLE));

    VkBuffer vb[] = { m_vertexBuffer };
    VkDeviceSize offsets[] = { 0 };
    vkCmdBindVertexBuffers(cmd, 0, 1, vb, offsets);
    vkCmdBindIndexBuffer(cmd, m_indexBuffer, 0, VK_INDEX_TYPE_UINT32);

    m_pRender->DrawIndexed(m_indexCount);
}

// ============================================================================
// 私有实现
// ============================================================================

void VulkanMesh::OnVertexBufferUploaded(VkBuffer staging, VkDeviceMemory stagingMem,
                                         VkDeviceSize bufferSize) {
    VkDevice device = m_pRender->GetDevice();

    // 创建 device-local 顶点缓冲
    VkBufferCreateInfo bi{};
    bi.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bi.size = bufferSize;
    bi.usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
    bi.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    VkBuffer devBuf = VK_NULL_HANDLE;
    if (vkCreateBuffer(device, &bi, nullptr, &devBuf) != VK_SUCCESS) {
        CleanupStaging(staging, stagingMem);
        return;
    }

    VkMemoryRequirements mr;
    vkGetBufferMemoryRequirements(device, devBuf, &mr);
    VkMemoryAllocateInfo ai{};
    ai.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    ai.allocationSize = mr.size;
    ai.memoryTypeIndex = FindMemoryType(mr.memoryTypeBits,
                                        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

    VkDeviceMemory devMem = VK_NULL_HANDLE;
    if (vkAllocateMemory(device, &ai, nullptr, &devMem) != VK_SUCCESS) {
        vkDestroyBuffer(device, devBuf, nullptr);
        CleanupStaging(staging, stagingMem);
        return;
    }
    vkBindBufferMemory(device, devBuf, devMem, 0);

    // staging → device-local
    VkCommandBuffer cmd = m_pRender->BeginSingleTimeCommands();
    VkBufferCopy region{};
    region.size = bufferSize;
    vkCmdCopyBuffer(cmd, staging, devBuf, 1, &region);
    m_pRender->EndSingleTimeCommands(cmd);

    // 清理 staging
    vkDestroyBuffer(device, staging, nullptr);
    vkFreeMemory(device, stagingMem, nullptr);

    m_vertexBuffer       = devBuf;
    m_vertexBufferMemory = devMem;
    CheckBuffersReady();
}

void VulkanMesh::OnIndexBufferUploaded(VkBuffer staging, VkDeviceMemory stagingMem,
                                        VkDeviceSize bufferSize) {
    VkDevice device = m_pRender->GetDevice();

    VkBufferCreateInfo bi{};
    bi.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bi.size = bufferSize;
    bi.usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_INDEX_BUFFER_BIT;
    bi.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    VkBuffer devBuf = VK_NULL_HANDLE;
    if (vkCreateBuffer(device, &bi, nullptr, &devBuf) != VK_SUCCESS) {
        CleanupStaging(staging, stagingMem);
        return;
    }

    VkMemoryRequirements mr;
    vkGetBufferMemoryRequirements(device, devBuf, &mr);
    VkMemoryAllocateInfo ai{};
    ai.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    ai.allocationSize = mr.size;
    ai.memoryTypeIndex = FindMemoryType(mr.memoryTypeBits,
                                        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

    VkDeviceMemory devMem = VK_NULL_HANDLE;
    if (vkAllocateMemory(device, &ai, nullptr, &devMem) != VK_SUCCESS) {
        vkDestroyBuffer(device, devBuf, nullptr);
        CleanupStaging(staging, stagingMem);
        return;
    }
    vkBindBufferMemory(device, devBuf, devMem, 0);

    VkCommandBuffer cmd = m_pRender->BeginSingleTimeCommands();
    VkBufferCopy region{};
    region.size = bufferSize;
    vkCmdCopyBuffer(cmd, staging, devBuf, 1, &region);
    m_pRender->EndSingleTimeCommands(cmd);

    vkDestroyBuffer(device, staging, nullptr);
    vkFreeMemory(device, stagingMem, nullptr);

    m_indexBuffer       = devBuf;
    m_indexBufferMemory = devMem;
    CheckBuffersReady();
}

void VulkanMesh::OnCombinedBuffersUploaded(
    VkBuffer staging, VkDeviceMemory stagingMem,
    const std::vector<BufferUploadRunnable::UploadSegment>& segments)
{
    if (segments.size() < 2) return;
    VkDevice device = m_pRender->GetDevice();

    // 辅助：从 staging buffer 的某个偏移创建 device-local buffer 并拷贝
    auto createDevBuf = [&](const BufferUploadRunnable::UploadSegment& seg,
                            VkBuffer& outBuf, VkDeviceMemory& outMem) {
        VkBufferCreateInfo bi{};
        bi.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        bi.size = seg.size;
        bi.usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT | seg.usage;
        bi.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

        VkBuffer devBuf = VK_NULL_HANDLE;
        if (vkCreateBuffer(device, &bi, nullptr, &devBuf) != VK_SUCCESS) return false;

        VkMemoryRequirements mr;
        vkGetBufferMemoryRequirements(device, devBuf, &mr);
        VkMemoryAllocateInfo ai{};
        ai.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        ai.allocationSize = mr.size;
        ai.memoryTypeIndex = FindMemoryType(mr.memoryTypeBits,
                                            VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

        VkDeviceMemory devMem = VK_NULL_HANDLE;
        if (vkAllocateMemory(device, &ai, nullptr, &devMem) != VK_SUCCESS) {
            vkDestroyBuffer(device, devBuf, nullptr);
            return false;
        }
        vkBindBufferMemory(device, devBuf, devMem, 0);

        VkCommandBuffer cmd = m_pRender->BeginSingleTimeCommands();
        VkBufferCopy region{};
        region.srcOffset = seg.stagingOffset;
        region.size = seg.size;
        vkCmdCopyBuffer(cmd, staging, devBuf, 1, &region);
        m_pRender->EndSingleTimeCommands(cmd);

        outBuf = devBuf;
        outMem = devMem;
        return true;
    };

    bool vertOk = createDevBuf(segments[0], m_vertexBuffer, m_vertexBufferMemory);
    bool idxOk  = createDevBuf(segments[1], m_indexBuffer, m_indexBufferMemory);

    // 清理 staging buffer
    vkDestroyBuffer(device, staging, nullptr);
    vkFreeMemory(device, stagingMem, nullptr);

    if (vertOk && idxOk) {
        m_buffersReady = true;
    }
}

void VulkanMesh::CheckBuffersReady() {
    if (m_vertexBuffer != VK_NULL_HANDLE && m_indexBuffer != VK_NULL_HANDLE) {
        m_buffersReady = true;
    }
}

void VulkanMesh::CleanupStaging(VkBuffer staging, VkDeviceMemory stagingMem) {
    if (!m_pRender) return;
    VkDevice device = m_pRender->GetDevice();
    if (staging != VK_NULL_HANDLE) vkDestroyBuffer(device, staging, nullptr);
    if (stagingMem != VK_NULL_HANDLE) vkFreeMemory(device, stagingMem, nullptr);
}
