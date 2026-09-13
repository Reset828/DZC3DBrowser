#ifndef __GL_OBJECT_H__
#define __GL_OBJECT_H__

#include "Object/Object.h"
#include <cstddef>
#include <cstdint>

class GLRender;

// OpenGL 场景对象：持有 VAO / VBO / EBO。
class GLObject : public Object {
public:
    GLObject();
    ~GLObject() override;

    GLObject(const GLObject&) = delete;
    GLObject& operator=(const GLObject&) = delete;

    // 绑定所属渲染器。
    void SetRender(GLRender* pRender);
    // 返回所属渲染器。
    GLRender* GetRender() const;

    // 创建并上传顶点缓冲区。
    void CreateVertexBuffer(const void* data, std::size_t size);
    // 创建并上传索引缓冲区。
    void CreateIndexBuffer(const void* data, std::size_t size, uint32_t indexCount);
    // 销毁对象持有的 GPU 缓冲区。
    void DestroyBuffers();

    // 返回 VAO。
    uint32_t GetVertexArray() const;
    // 返回索引数量。
    uint32_t GetIndexCount() const;

protected:
    GLRender* m_pRender = nullptr;
    uint32_t m_vao = 0;
    uint32_t m_vbo = 0;
    uint32_t m_ebo = 0;
    uint32_t m_indexCount = 0;
};

#endif //__GL_OBJECT_H__
