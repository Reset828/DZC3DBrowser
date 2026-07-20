#ifndef __VULKAN_OBJECT_H__
#define __VULKAN_OBJECT_H__

#include "render/VulkanRender.h"
#include "VertexTypes.h"
#include <cstdint>

class VulkanObject {
public:
    VulkanObject();
    VulkanObject(const VulkanObject& obj);
    virtual ~VulkanObject();

    VulkanObject& operator=(const VulkanObject& obj);

    enum ObjectType {
        OT_OBJECT = 0,
        OT_LINE,
        OT_LAYER
    };

    enum FlagType {
        FT_VISIBLE = 1,
        FT_DIRTY = 16,
    };



    void SetRender(VulkanRender* pRender);
    VulkanRender* GetRender() const;

    void SetParent(VulkanObject* pParent);
    VulkanObject* GetParent() const;

    void SetDirty(bool bDirty);
    bool IsDirty() const;

    uint32_t GetType() const;

    void SetVisible(bool bVisible);
    bool IsVisible() const;

    void SetColor(const Vec4& clr);
    void SetColor(float r, float g, float b, float a);
    Vec4 GetColor() const;

    virtual void Render(int iMode = 0) = 0;

void CreateVertexBuffer(const std::vector<Vertex3D>& vertices);  // 创建顶点缓冲区
void CreateIndexBuffer(const std::vector<uint32_t>& indices);        // 创建索引缓冲区
void UpdateVertexBuffer(const std::vector<Vertex3D>& vertices);  // 更新顶点缓冲区
    void UpdateIndexBuffer(const std::vector<uint32_t>& indices);        // 更新索引缓冲区
    void DestroyBuffers();  // 销毁缓冲区

    VkBuffer GetVertexBuffer() const;
    VkBuffer GetIndexBuffer() const;
    uint32_t GetIndexCount() const;

    void CreateBufferHelper(VkDeviceSize size, VkBufferUsageFlags usage,
                           VkMemoryPropertyFlags properties,
                           VkBuffer& buffer, VkDeviceMemory& bufferMemory);
    uint32_t FindMemoryType(uint32_t typeFilter, VkMemoryPropertyFlags properties);
    void CopyBuffer(VkBuffer srcBuffer, VkBuffer dstBuffer, VkDeviceSize size);

protected:
    uint8_t m_uType = OT_OBJECT;          // 对象类型

    VulkanObject* m_pParent = nullptr;

    VulkanRender* m_pRender = nullptr;    // 关联的渲染器

    uint8_t m_uFlag = FT_VISIBLE;

    uint32_t m_uClr = 0xFFFFFFFF;

    VkBuffer m_vertexBuffer = VK_NULL_HANDLE;           // 顶点缓冲区
    VkDeviceMemory m_vertexBufferMemory = VK_NULL_HANDLE;  // 顶点缓冲区内存
    VkBuffer m_indexBuffer = VK_NULL_HANDLE;            // 索引缓冲区
    VkDeviceMemory m_indexBufferMemory = VK_NULL_HANDLE;   // 索引缓冲区内存
    uint32_t m_indexCount = 0;

private:
    void EnableFlag(FlagType ft, bool bEnable);
    bool IsFlagEnabled(FlagType ft) const;
};

#endif //__VULKAN_OBJECT_H__
