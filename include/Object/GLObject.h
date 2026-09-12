#ifndef __GL_OBJECT_H__
#define __GL_OBJECT_H__

#include "Object/Object.h"
#include "VertexType/VertexTypes.h"
#include <cstdint>
#include <vector>

class GLRender;

class GLObject : public Object {
public:
    GLObject();
    ~GLObject() override;

    GLObject(const GLObject&) = delete;
    GLObject& operator=(const GLObject&) = delete;

    void SetRender(GLRender* pRender);
    GLRender* GetRender() const;

    void CreateVertexBuffer(const std::vector<Vertex3D>& vertices);
    void CreateIndexBuffer(const std::vector<uint32_t>& indices);
    void DestroyBuffers();

    uint32_t GetVertexArray() const;
    uint32_t GetIndexCount() const;

protected:
    GLRender* m_pRender = nullptr;
    uint32_t m_vao = 0;
    uint32_t m_vbo = 0;
    uint32_t m_ebo = 0;
    uint32_t m_indexCount = 0;
};

#endif //__GL_OBJECT_H__
