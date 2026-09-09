#version 450
layout(std140, binding = 0) uniform UniformBufferObject {
    mat4 model;
    mat4 view;
    mat4 proj;
    vec4 displayOptions;
    vec4 sunDirection;
} ubo;

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inColor;
layout(location = 2) in vec2 inTexCoord;
layout(location = 3) in vec3 inNormal;

layout(location = 0) out vec3 fragColor;
layout(location = 1) out vec3 fragViewPosition;
layout(location = 2) flat out float grayEnabled;
layout(location = 3) out vec3 fragViewNormal;
layout(location = 4) out vec3 fragWorldPosition;
layout(location = 5) flat out float dyeEnabled;
layout(location = 6) flat out float lightAnalysisEnabled;
layout(location = 7) flat out float sunAboveHorizon;
layout(location = 8) flat out vec3 fragSunDirection;
layout(location = 9) out vec3 fragObjectPosition;
layout(location = 10) out vec3 fragObjectNormal;

void main() {
    vec4 worldPosition = ubo.model * vec4(inPosition, 1.0);
    vec4 viewPosition = ubo.view * worldPosition;
    gl_Position = ubo.proj * viewPosition;
    gl_PointSize = 8.0;

    fragColor = vec3(0.0);
    fragViewPosition = viewPosition.xyz;
    fragWorldPosition = worldPosition.xyz;
    grayEnabled = ubo.displayOptions.x;
    dyeEnabled = ubo.displayOptions.y;
    lightAnalysisEnabled = ubo.displayOptions.z;
    sunAboveHorizon = ubo.displayOptions.w;
    fragSunDirection = ubo.sunDirection.xyz;
    fragObjectPosition = inPosition;
    fragObjectNormal = inNormal;

    mat3 normalMatrix = transpose(inverse(mat3(ubo.view * ubo.model)));
    fragViewNormal = normalMatrix * inNormal;
}
