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

// Vertex3D <-> GLSL locations (keep in sync with VertexTypes.cpp / GLMesh):
//   0 position vec3, 1 color vec3, 2 texCoord vec2, 3 normal vec3, 4 tangent vec4
// Color path: inColor is interpolated as fragColor for the fragment shader.
// Tangent path (1.5): inTangent.xyz = tangent, inTangent.w = bitangent sign (handedness).
layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inColor;
layout(location = 2) in vec2 inTexCoord;
layout(location = 3) in vec3 inNormal;
layout(location = 4) in vec4 inTangent;

layout(location = 0) out vec3 fragViewPosition;
layout(location = 1) out vec3 fragViewNormal;
layout(location = 2) out vec3 fragWorldPosition;
layout(location = 3) out vec3 fragObjectPosition;
layout(location = 4) out vec3 fragObjectNormal;
layout(location = 5) out vec3 fragColor;
layout(location = 6) out vec2 fragTexCoord;
// 1.5：物体空间切线（TBN 基）。w 分量复用位置传手性，避免额外插值通道：
// 用单独 vec4 承载切线 xyz + 手性，直接传给片元做 TBN。
layout(location = 7) out vec4 fragObjectTangent;

void main() {
    vec4 worldPosition = ubo.model * vec4(inPosition, 1.0);
    vec4 viewPosition = ubo.view * worldPosition;
    gl_Position = ubo.proj * viewPosition;
    gl_PointSize = 8.0;

    fragViewPosition = viewPosition.xyz;
    fragWorldPosition = worldPosition.xyz;
    fragObjectPosition = inPosition;
    fragObjectNormal = inNormal;
    fragColor = inColor;
    fragTexCoord = inTexCoord;
    fragObjectTangent = inTangent;

    mat3 normalMatrix = transpose(inverse(mat3(ubo.view * ubo.model)));
    fragViewNormal = normalMatrix * inNormal;
}
