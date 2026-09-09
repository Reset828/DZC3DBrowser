#ifndef __VULKAN_OBJECT_H__
#define __VULKAN_OBJECT_H__

#include "Object/SceneObject.h"
#include "Render/VulkanRender.h"
#include "VertexType/VertexTypes.h"
#include <cstdint>
#include <vector>

class VulkanObject : public SceneObject {
public:
    VulkanObject();
    VulkanObject(const VulkanObject& obj);
    ~VulkanObject() override;

    VulkanObject& operator=(const VulkanObject& obj);

    void SetRender(VulkanRender* pRender);
    VulkanRender* GetRender() const;

    void CreateVertexBuffer(const std::vector<Vertex3D>& vertices);
    void CreateIndexBuffer(const std::vector<uint32_t>& indices);
    void UpdateVertexBuffer(const std::vector<Vertex3D>& vertices);
    void UpdateIndexBuffer(const std::vector<uint32_t>& indices);
    void DestroyBuffers();

    VkBuffer GetVertexBuffer() const;
    VkBuffer GetIndexBuffer() const;
    uint32_t GetIndexCount() const;

    void CreateBufferHelper(VkDeviceSize size, VkBufferUsageFlags usage,
                           VkMemoryPropertyFlags properties,
                           VkBuffer& buffer, VkDeviceMemory& bufferMemory);
    uint32_t FindMemoryType(uint32_t typeFilter, VkMemoryPropertyFlags properties);
    void CopyBuffer(VkBuffer srcBuffer, VkBuffer dstBuffer, VkDeviceSize size);

protected:
    VulkanRender* m_pRender = nullptr;
    VkBuffer m_vertexBuffer = VK_NULL_HANDLE;
    VkDeviceMemory m_vertexBufferMemory = VK_NULL_HANDLE;
    VkBuffer m_indexBuffer = VK_NULL_HANDLE;
    VkDeviceMemory m_indexBufferMemory = VK_NULL_HANDLE;
    uint32_t m_indexCount = 0;
};

#endif //__VULKAN_OBJECT_H__
