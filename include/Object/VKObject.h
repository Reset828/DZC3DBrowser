#ifndef __VK_OBJECT_H__
#define __VK_OBJECT_H__

#include "Object/Object.h"
#include <vulkan/vulkan.h>
#include <cstddef>
#include <cstdint>

class VKRender;

// Vulkan 场景对象：持有 vertex / index buffer。
class VKObject : public Object {
public:
    VKObject();
    VKObject(const VKObject& obj);
    ~VKObject() override;

// operator=：复制对象的公共状态和渲染器引用。
    VKObject& operator=(const VKObject& obj);

    // 绑定所属渲染器。
    void SetRender(VKRender* pRender);
    // 返回所属渲染器。
    VKRender* GetRender() const;

    // 创建顶点缓冲区。
    void CreateVertexBuffer(const void* data, VkDeviceSize size);
    // 创建索引缓冲区。
    void CreateIndexBuffer(const void* data, VkDeviceSize size, uint32_t indexCount);
    // 更新已有顶点缓冲区内容。
    void UpdateVertexBuffer(const void* data, VkDeviceSize size);
    // 更新已有索引缓冲区内容。
    void UpdateIndexBuffer(const void* data, VkDeviceSize size, uint32_t indexCount);
    // 销毁对象持有的 GPU 缓冲区。
    void DestroyBuffers();

    // 返回顶点缓冲区句柄。
    VkBuffer GetVertexBuffer() const;
    // 返回索引缓冲区句柄。
    VkBuffer GetIndexBuffer() const;
    // 返回索引数量。
    uint32_t GetIndexCount() const;


protected:
    VKRender* m_pRender = nullptr;
    VkBuffer m_vertexBuffer = VK_NULL_HANDLE;
    VkBuffer m_indexBuffer = VK_NULL_HANDLE;
    uint32_t m_indexCount = 0;

private:
    // 经 staging 创建设备本地缓冲区。
    void CreateBufferFromData(const void* data, VkDeviceSize size,
                              VkBufferUsageFlags usage, VkBuffer& target);
    // 把数据写入已有缓冲区。
    void UpdateBufferFromData(const void* data, VkDeviceSize size, VkBuffer target);
};

#endif //__VK_OBJECT_H__
