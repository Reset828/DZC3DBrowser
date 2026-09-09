#ifndef __VERTEX_TYPES_H__
#define __VERTEX_TYPES_H__

#include <vulkan/vulkan.h>
#include <vector>
#include <cstdint>
#include <cstddef>

struct Vertex2D {
    float position[2];
    float color[3];
    float texCoord[2];

    static VkVertexInputBindingDescription GetBindingDescription();
    static std::vector<VkVertexInputAttributeDescription> GetAttributeDescriptions();
};

struct Vertex3D {
    float position[3];
    float color[3];
    float texCoord[2];
    float normal[3];

    static VkVertexInputBindingDescription GetBindingDescription();
    static std::vector<VkVertexInputAttributeDescription> GetAttributeDescriptions();
};

struct UniformBufferObject3D {
    alignas(16) float model[4][4];
    alignas(16) float view[4][4];
    alignas(16) float proj[4][4];
    alignas(16) float displayOptions[4]; // x: 灰度  y: 染色  z: 光照分析  w: 太阳在地平线以上
    alignas(16) float sunDirection[4];   // xyz: 物体空间指向太阳（+X 东 +Y 北 +Z 上）
};

#endif //__VERTEX_TYPES_H__
