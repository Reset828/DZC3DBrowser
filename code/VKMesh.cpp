#include "VKMesh.h"
#include "BufferUploadRunnable.h"
#include "Render/VKRender.h"

VKMesh::VKMesh()
    : m_alive(std::make_shared<std::atomic<bool>>(true))
{
    m_uType = OT_OBJECT;
}

VKMesh::~VKMesh() {
    *m_alive = false; // 标记已销毁，阻止未执行的异步回调继续
}


// 启动 Mesh 数据的异步上传。
void VKMesh::SetMeshData(std::vector<Vertex3D>&& vertices,
                              std::vector<uint32_t>&& indices) {
    if (!m_pRender || m_pRender->IsShuttingDown() || m_pRender->IsDeviceLost()
        || vertices.empty() || indices.empty()) return;

    DestroyBuffers();
    m_buffersReady = false;

    VkDevice device         = m_pRender->GetDevice();
    VkPhysicalDevice phyDev = m_pRender->GetPhysicalDevice();

    VkDeviceSize vertSize = vertices.size() * sizeof(Vertex3D);
    VkDeviceSize idxSize  = indices.size()  * sizeof(uint32_t);
    m_indexCount = static_cast<uint32_t>(indices.size());

    auto vertData = std::make_shared<std::vector<Vertex3D>>(std::move(vertices));
    auto idxData  = std::make_shared<std::vector<uint32_t>>(std::move(indices));

    auto alive = m_alive;

    ++m_uploadGeneration;
    uint64_t gen = m_uploadGeneration;

    std::vector<BufferUploadRunnable::UploadSegment> segments;
    segments.push_back({ vertData->data(), vertSize, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT });
    segments.push_back({ idxData->data(),  idxSize,  VK_BUFFER_USAGE_INDEX_BUFFER_BIT });

    auto task = new BufferUploadRunnable(device, phyDev, std::move(segments),
        [alive, this, device, vertData, idxData, gen](VkBuffer staging,
                                                                VkDeviceMemory stagingMem,
            const std::vector<BufferUploadRunnable::UploadSegment>& resultSegs) {
            if (!*alive) {
                if (device != VK_NULL_HANDLE) {
                    vkDestroyBuffer(device, staging, nullptr);
                    vkFreeMemory(device, stagingMem, nullptr);
                }
                return;
            }
            if (!m_pRender || m_pRender->IsShuttingDown() || gen != m_uploadGeneration) {
                CleanupStaging(staging, stagingMem);
                return;
            }
            OnCombinedBuffersUploaded(staging, stagingMem, resultSegs);
        });

    m_pRender->SubmitAsync(task);
}

// 同步上传 Mesh 数据。
void VKMesh::SetMeshDataSync(const std::vector<Vertex3D>& vertices,
                                  const std::vector<uint32_t>& indices) {
    if (!m_pRender || vertices.empty() || indices.empty()) return;

    DestroyBuffers();
    m_buffersReady = false;

    CreateVertexBuffer(vertices.data(),
                       vertices.size() * sizeof(Vertex3D));
    CreateIndexBuffer(indices.data(),
                      indices.size() * sizeof(uint32_t),
                      static_cast<uint32_t>(indices.size()));
    m_buffersReady = true;
}

// 绑定管线与缓冲并按当前模式绘制。
void VKMesh::Render(int mode) {
    if (!IsVisible() || !m_buffersReady || !m_pRender) return;
    if (m_indexCount == 0) return;

    VkCommandBuffer cmd = m_pRender->GetCurrentCommandBuffer();
    VkPipeline pipeline = VK_NULL_HANDLE;
    if (mode == Object::RM_SHADOW) {
        pipeline = m_pRender->GetShadowPipeline();
    } else {
        pipeline = m_pRender->GetPipeline(m_pRender->IsWireframeEnabled()
            ? VKRender::DT_TRIANGLE_WIREFRAME
            : VKRender::DT_TRIANGLE);
    }
    if (pipeline == VK_NULL_HANDLE) return;

    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);

    VkBuffer vb[] = { m_vertexBuffer };
    VkDeviceSize offsets[] = { 0 };
    vkCmdBindVertexBuffers(cmd, 0, 1, vb, offsets);
    vkCmdBindIndexBuffer(cmd, m_indexBuffer, 0, VK_INDEX_TYPE_UINT32);

    m_pRender->DrawIndexed(m_indexCount);
}


// staging 完成后创建设备缓冲。
void VKMesh::OnCombinedBuffersUploaded(
    VkBuffer staging, VkDeviceMemory stagingMem,
    const std::vector<BufferUploadRunnable::UploadSegment>& segments)
{
    if (!m_pRender || m_pRender->IsShuttingDown() || segments.size() < 2) {
        CleanupStaging(staging, stagingMem);
        return;
    }

    VkBuffer vertexBuffer = VK_NULL_HANDLE;
    VkBuffer indexBuffer = VK_NULL_HANDLE;
    try {
        vertexBuffer = m_pRender->CreateBufferFromStaging(
            staging, segments[0].size, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
            segments[0].stagingOffset);
        indexBuffer = m_pRender->CreateBufferFromStaging(
            staging, segments[1].size, VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
            segments[1].stagingOffset);
    } catch (...) {
        if (vertexBuffer != VK_NULL_HANDLE) {
            m_pRender->DestroyBuffer(vertexBuffer);
        }
        if (indexBuffer != VK_NULL_HANDLE) {
            m_pRender->DestroyBuffer(indexBuffer);
        }
        CleanupStaging(staging, stagingMem);
        return;
    }

    CleanupStaging(staging, stagingMem);
    m_vertexBuffer = vertexBuffer;
    m_indexBuffer = indexBuffer;
    m_buffersReady = true;
}

// 顶点与索引缓冲都就绪后置位。
void VKMesh::CheckBuffersReady() {
    if (m_vertexBuffer != VK_NULL_HANDLE && m_indexBuffer != VK_NULL_HANDLE) {
        m_buffersReady = true;
    }
}

// 销毁 staging 缓冲与内存。
void VKMesh::CleanupStaging(VkBuffer staging, VkDeviceMemory stagingMem) {
    if (!m_pRender) return;
    VkDevice device = m_pRender->GetDevice();
    if (staging != VK_NULL_HANDLE) vkDestroyBuffer(device, staging, nullptr);
    if (stagingMem != VK_NULL_HANDLE) vkFreeMemory(device, stagingMem, nullptr);
}
