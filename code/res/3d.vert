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

// 统一 push constant 块（Vulkan）：材质参数（fragment 使用，offset 0..64）
// + 逐对象世界矩阵（vertex 使用，offset 64..128）。
// 在 vertex / fragment 两个阶段声明必须一致，故三份 3D 着色器共用同一块。
// OpenGL 无 push constant：材质用普通 uniform，对象矩阵用 uObjectModel。
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
    // 逐对象世界矩阵（任务 2.1）-> 归一化场景空间（着色/阴影/测量公共空间）。
    vec4 objectPosition = OBJECT_MODEL * vec4(inPosition, 1.0);
    // 显示变换（固定朝向 + 轨道相机）-> 显示世界坐标。
    vec4 worldPosition = ubo.model * objectPosition;
    vec4 viewPosition = ubo.view * worldPosition;
    gl_Position = ubo.proj * viewPosition;
    gl_PointSize = 8.0;

    fragViewPosition = viewPosition.xyz;
    fragWorldPosition = worldPosition.xyz;
    // 着色空间 = 归一化场景空间：太阳方向、阴影矩阵、相机位置都在此空间。
    fragObjectPosition = objectPosition.xyz;
    // 法线用逆转置矩阵（非均匀缩放下保持垂直）；切线按模型矩阵方向变换。
    mat3 objectNormalMatrix = transpose(inverse(mat3(OBJECT_MODEL)));
    fragObjectNormal = objectNormalMatrix * inNormal;
    fragColor = inColor;
    fragTexCoord = inTexCoord;
    fragObjectTangent = vec4(mat3(OBJECT_MODEL) * inTangent.xyz, inTangent.w);

    mat3 normalMatrix = transpose(inverse(mat3(ubo.view * ubo.model * OBJECT_MODEL)));
    fragViewNormal = normalMatrix * inNormal;
}
