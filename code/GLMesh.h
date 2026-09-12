#pragma once

#include "Object/GLObject.h"
#include "VertexType/VertexTypes.h"
#include <vector>

class GLMesh : public GLObject {
public:
    GLMesh();
    ~GLMesh() override;

    void SetMeshDataSync(const std::vector<Vertex3D>& vertices,
                         const std::vector<uint32_t>& indices);

    void Render(int mode = 0) override;

private:
    bool m_buffersReady = false;
};
