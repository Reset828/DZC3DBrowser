#version 450
layout(std140, binding = 0) uniform UniformBufferObject {
    mat4 model;
    mat4 view;
    mat4 proj;
    vec4 displayOptions;
    vec4 sunDirection;
    mat4 lightViewProj;
    vec4 shadowOptions;
} ubo;

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inColor;
layout(location = 2) in vec2 inTexCoord;
layout(location = 3) in vec3 inNormal;

layout(location = 0) out vec3 fragViewPosition;
layout(location = 1) out vec3 fragViewNormal;
layout(location = 2) out vec3 fragWorldPosition;
layout(location = 3) out vec3 fragObjectPosition;
layout(location = 4) out vec3 fragObjectNormal;

void main() {
    vec4 worldPosition = ubo.model * vec4(inPosition, 1.0);
    vec4 viewPosition = ubo.view * worldPosition;
    gl_Position = ubo.proj * viewPosition;
    gl_PointSize = 8.0;

    fragViewPosition = viewPosition.xyz;
    fragWorldPosition = worldPosition.xyz;
    fragObjectPosition = inPosition;
    fragObjectNormal = inNormal;

    mat3 normalMatrix = transpose(inverse(mat3(ubo.view * ubo.model)));
    fragViewNormal = normalMatrix * inNormal;
}
