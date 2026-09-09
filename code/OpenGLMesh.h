#pragma once

#include "Object/OpenGLObject.h"
#include "VertexType/VertexTypes.h"
#include <vector>

class OpenGLMesh : public OpenGLObject {
public:
    OpenGLMesh();
    ~OpenGLMesh() override;

    void SetMeshDataSync(const std::vector<Vertex3D>& vertices,
                         const std::vector<uint32_t>& indices);

    void Render(int mode = 0) override;

private:
    bool m_buffersReady = false;
};
