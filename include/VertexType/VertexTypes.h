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

    // 返回顶点绑定描述。
    static VkVertexInputBindingDescription GetBindingDescription();
    // 返回顶点属性描述。
    static std::vector<VkVertexInputAttributeDescription> GetAttributeDescriptions();
};

struct Vertex3D {
    float position[3];
    float color[3];
    float texCoord[2];
    float normal[3];
    // 切线空间基（1.5）：xyz = 切线，w = 副切线手性符号。location 4。
    float tangent[4];

    // 返回顶点绑定描述。
    static VkVertexInputBindingDescription GetBindingDescription();
    // 返回顶点属性描述。
    static std::vector<VkVertexInputAttributeDescription> GetAttributeDescriptions();
};

struct UniformBufferObject3D {
    alignas(16) float model[4][4];
    alignas(16) float view[4][4];
    alignas(16) float proj[4][4];
    alignas(16) float displayOptions[4]; // x: 灰度  y: 染色  z: 光照分析  w: 太阳在地平线以上
    alignas(16) float sunDirection[4];   // xyz: 物体空间指向太阳（+X 东 +Y 北 +Z 上）
    alignas(16) float lightViewProj[4][4];
    alignas(16) float shadowOptions[4];  // x: 阴影贴图可用  y: NDC深度从[-1,1]转到[0,1]  z: 线框模式  w: Vulkan 窗口Y向下标志（取反 dFdy）
    alignas(16) float cameraObjectPosition[4]; // xyz: 相机在物体空间的位置（PBR 视线向量），w 未用
    // 1.7：半球环境光 + 阴影偏移/PCF + 调试视图。
    alignas(16) float ambientSkyColor[4];      // xyz: 天空色(线性)  w: 环境光强度
    alignas(16) float ambientGroundColor[4];   // xyz: 地面色(线性)  w 未用
    alignas(16) float shadowParams[4];         // x: 基础深度偏移  y: PCF 档位(0关/1=3x3/2=5x5)  z: 世界纹素尺寸(法线偏移用)  w 未用
    alignas(16) float debugOptions[4];         // x: 调试视图(0正常/1深度/2世界法线/3阴影)  yzw 未用
    alignas(16) float depthRange[4];           // x: 近平面  y: 远平面  zw 未用（线性深度归一化用）
};

#endif //__VERTEX_TYPES_H__
