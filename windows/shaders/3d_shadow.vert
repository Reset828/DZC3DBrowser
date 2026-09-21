#version 450
layout(std140, binding = 0) uniform UniformBufferObject {
    mat4 model;
    mat4 view;
    mat4 proj;
    vec4 displayOptions;
    vec4 sunDirection;
    mat4 lightViewProj;
    vec4 shadowOptions;
    vec4 cameraWorldPosition;
    vec4 ambientSkyColor;
    vec4 ambientGroundColor;
    vec4 shadowParams;
    vec4 debugOptions;
    vec4 depthRange;
} ubo;

// 统一 push constant 块（Vulkan）：材质参数（前 64 字节）+ 逐对象世界矩阵（offset 64）。
// 必须与 3d.vert / 3d.frag 的声明一致。OpenGL 用普通 uniform uObjectModel。
#ifdef VULKAN
layout(push_constant) uniform PushConstants {
    vec4 baseColor;
    vec4 emissiveFactor;
    float alphaCutoff;
    float metallic;
    float roughness;
    float normalScale;
    float occlusionStrength;
    float emissiveStrength;
    int alphaMode;
    int pad0;
    mat4 objectModel;
} pc;
#define OBJECT_MODEL pc.objectModel
#else
uniform mat4 uObjectModel;
#define OBJECT_MODEL uObjectModel
#endif

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inColor;
layout(location = 2) in vec2 inTexCoord;
layout(location = 3) in vec3 inNormal;
layout(location = 4) in vec4 inTangent;

void main() {
    // 先变换到归一化场景空间，再用光源矩阵投影（与主通道的着色空间一致）。
    gl_Position = ubo.lightViewProj * OBJECT_MODEL * vec4(inPosition, 1.0);
}
