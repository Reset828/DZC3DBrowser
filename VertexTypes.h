#ifndef __VERTEX_TYPES_H__
#define __VERTEX_TYPES_H__

#include <vulkan/vulkan.h>
#include <vector>
#include <cstdint>
#include <cstddef>

// 2D顶点格式（Vec2 位置）
struct Vertex2D {
    float position[2];
    float color[3];
    float texCoord[2];

    static VkVertexInputBindingDescription GetBindingDescription();
    static std::vector<VkVertexInputAttributeDescription> GetAttributeDescriptions();
};

// 3D顶点格式（Vec3 位置）
struct Vertex3D {
    float position[3];
    float color[3];
    float texCoord[2];

    static VkVertexInputBindingDescription GetBindingDescription();
    static std::vector<VkVertexInputAttributeDescription> GetAttributeDescriptions();
};

// 3D UBO（MVP矩阵）
struct UniformBufferObject3D {
    alignas(16) float model[4][4];
    alignas(16) float view[4][4];
    alignas(16) float proj[4][4];
    alignas(16) float displayOptions[4]; // x: 是否按局部 Z 显示灰度
};

#endif //__VERTEX_TYPES_H__
