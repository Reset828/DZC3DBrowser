#version 450
layout(binding = 0) uniform UniformBufferObject {
    mat4 model;
    mat4 view;
    mat4 proj;
    vec4 displayOptions;
} ubo;

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inColor;
layout(location = 2) in vec2 inTexCoord;
layout(location = 3) in vec3 inNormal;

layout(location = 0) out vec3 fragColor;
layout(location = 1) out vec3 fragViewPosition;
layout(location = 2) flat out float grayEnabled;
layout(location = 3) out vec3 fragViewNormal;

void main() {
    vec4 viewPosition = ubo.view * ubo.model * vec4(inPosition, 1.0);
    gl_Position = ubo.proj * viewPosition;
    gl_PointSize = 8.0;

    // The default display remains pure black.
    fragColor = vec3(0.0);
    fragViewPosition = viewPosition.xyz;
    grayEnabled = ubo.displayOptions.x;

    mat3 normalMatrix = transpose(inverse(mat3(ubo.view * ubo.model)));
    // 零向量表示 OBJ 未提供法线，片元着色器将按三角面重建法线。
    fragViewNormal = normalMatrix * inNormal;
}
