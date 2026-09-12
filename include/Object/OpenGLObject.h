#ifndef __OPENGL_OBJECT_H__
#define __OPENGL_OBJECT_H__

#include "Object/SceneObject.h"
#include "VertexType/VertexTypes.h"
#include <cstdint>
#include <vector>

class GLRender;

class OpenGLObject : public SceneObject {
public:
    OpenGLObject();
    ~OpenGLObject() override;

    OpenGLObject(const OpenGLObject&) = delete;
    OpenGLObject& operator=(const OpenGLObject&) = delete;

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

#endif //__OPENGL_OBJECT_H__
