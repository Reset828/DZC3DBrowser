#version 450
// Gizmo 叠加层顶点着色器（任务 2.4）。
// 顶点已在“归一化场景空间”（= 着色空间），故与 3d.vert 共用同一显示变换
// （ubo.model 固定朝向 + 轨道相机）与 view/proj，直接输出。
// 顶点格式：position vec3 + color rgba（无光照，颜色直接插值到片元）。
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

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec4 inColor;

layout(location = 0) out vec4 fragColor;

void main() {
    gl_Position = ubo.proj * ubo.view * ubo.model * vec4(inPosition, 1.0);
    fragColor = inColor;
}
