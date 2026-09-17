#pragma once

#include <cstdint>
#include <vector>
#include "Asset/MeshData.h"
#include "VertexType/VertexTypes.h"

// 把资产网格转成渲染器使用的 Vertex3D 顶点与索引数组。
void BuildVertex3DArrays(const MeshData& mesh,
                         std::vector<Vertex3D>& outVertices,
                         std::vector<uint32_t>& outIndices);
