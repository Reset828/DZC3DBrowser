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

void VulkanMesh::SetMeshData(std::vector<VulkanVertex>&& vertices,
                              std::vector<uint32_t>&& indices) {
    if (!m_pRender || vertices.empty() || indices.empty()) return;

    // 清理旧缓冲
    DestroyBuffers();
    m_buffersReady = false;

    VkDevice device         = m_pRender->GetDevice();
    VkPhysicalDevice phyDev = m_pRender->GetPhysicalDevice();

    VkDeviceSize vertSize = vertices.size() * sizeof(VulkanVertex);
    VkDeviceSize idxSize  = indices.size()  * sizeof(uint32_t);
    m_indexCount = static_cast<uint32_t>(indices.size());

    // 保持原始数据在 QRunnable 执行期间存活
    auto vertData = std::make_shared<std::vector<VulkanVertex>>(std::move(vertices));
    auto idxData  = std::make_shared<std::vector<uint32_t>>(std::move(indices));

    // 生命周期令牌（值拷贝，使 lambda 可拷贝）
    auto alive = m_alive;

    // 顶点缓冲上传
    auto vertTask = new BufferUploadRunnable(
        device, phyDev,
        vertData->data(), vertSize,
        VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
        [alive, this, vertData, vertSize](VkBuffer staging, VkDeviceMemory stagingMem) {
            if (!*alive) {
                CleanupStaging(staging, stagingMem);
                return;
            }
            OnVertexBufferUploaded(staging, stagingMem, vertSize);
        });

    // 索引缓冲上传
    auto idxTask = new BufferUploadRunnable(
        device, phyDev,
        idxData->data(), idxSize,
        VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
        [alive, this, idxData, idxSize](VkBuffer staging, VkDeviceMemory stagingMem) {
            if (!*alive) {
                CleanupStaging(staging, stagingMem);
                return;
            }
            OnIndexBufferUploaded(staging, stagingMem, idxSize);
        });

    m_pRender->SubmitAsync(vertTask);
    m_pRender->SubmitAsync(idxTask);
}

void VulkanMesh::SetMeshDataSync(const std::vector<VulkanVertex>& vertices,
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

    // 绑定三角形管线（与 BeginFrame 中的默认管线一致，但在 CEditLine
    // 切换拓扑后需要显式恢复）
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                      m_pRender->GetPipeline(VulkanRender::DT_TRIANGLE));

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
