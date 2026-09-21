#version 450
layout(std140, binding = 0) uniform UniformBufferObject {
    mat4 model;
    mat4 view;
    mat4 proj;
    vec4 displayOptions;
    vec4 sunDirection;
    mat4 lightViewProj;
    vec4 shadowOptions;
    vec4 cameraObjectPosition;
    vec4 ambientSkyColor;
    vec4 ambientGroundColor;
    vec4 shadowParams;
    vec4 debugOptions;
    vec4 depthRange;
} ubo;

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inColor;
layout(location = 2) in vec2 inTexCoord;
layout(location = 3) in vec3 inNormal;
layout(location = 4) in vec4 inTangent;

void main() {
    gl_Position = ubo.lightViewProj * vec4(inPosition, 1.0);
}
