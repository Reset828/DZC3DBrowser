#version 450
layout(std140, binding = 0) uniform UniformBufferObject {
    mat4 model;
    mat4 view;
    mat4 proj;
    vec4 displayOptions;
    vec4 sunDirection;
    mat4 lightViewProj;
    vec4 shadowOptions;
    vec4 cameraWorldPosition;  // xyz: 相机在归一化场景空间的位置（PBR 视线向量），w 未用
    vec4 ambientSkyColor;      // 1.7: xyz 天空色(线性), w 环境光强度
    vec4 ambientGroundColor;   // 1.7: xyz 地面色(线性), w 未用
    vec4 shadowParams;         // 1.7: x 基础深度偏移, y PCF 档位, z 世界纹素尺寸, w 未用
    vec4 debugOptions;         // 1.7: x 调试视图(0正常/1深度/2世界法线/3阴影), yzw 未用
    vec4 depthRange;           // 1.7: x 近平面, y 远平面, zw 未用
} ubo;

layout(binding = 1) uniform sampler2DShadow shadowMap;

// 材质参数（1.4 / 1.5）。
//   Vulkan：与 3d.vert 共用同一个 push constant 块（前 64 字节为材质；后 64 字节为
//           逐对象世界矩阵，本阶段不用但必须声明一致）。
//   OpenGL：普通 uniform + 纹理单元 2..6。
// 颜色空间约定：baseColor 纹理与 emissive 纹理为 sRGB（采样时硬件解码为线性），
// 其余（metallicRoughness / normal / occlusion）为线性数据贴图。所有光照计算在线性空间，
// 最终写入 sRGB 交换链由硬件编码，切勿在着色器里手动做 gamma。
#ifdef VULKAN
layout(push_constant) uniform PushConstants {
    vec4 baseColor;          // rgb 基础色(线性) + a 材质 alpha
    vec4 emissiveFactor;     // xyz 线性自发光色
    float alphaCutoff;
    float metallic;          // [0,1]
    float roughness;         // [0,1]
    float normalScale;
    float occlusionStrength; // [0,1]
    float emissiveStrength;
    int alphaMode;           // 0 Opaque / 1 Mask / 2 Blend
    int highlight;           // 选中高亮标志（0 否 / 1 是，任务 2.3）
    mat4 objectModel;        // 逐对象世界矩阵（vertex 阶段使用）
} mat;
layout(set = 1, binding = 0) uniform sampler2D baseColorTexture;
layout(set = 1, binding = 1) uniform sampler2D metallicRoughnessTexture;
layout(set = 1, binding = 2) uniform sampler2D normalTexture;
layout(set = 1, binding = 3) uniform sampler2D occlusionTexture;
layout(set = 1, binding = 4) uniform sampler2D emissiveTexture;
#define MAT_BASECOLOR mat.baseColor
#define MAT_EMISSIVE mat.emissiveFactor
#define MAT_ALPHACUTOFF mat.alphaCutoff
#define MAT_METALLIC mat.metallic
#define MAT_ROUGHNESS mat.roughness
#define MAT_NORMALSCALE mat.normalScale
#define MAT_OCCLUSIONSTRENGTH mat.occlusionStrength
#define MAT_EMISSIVESTRENGTH mat.emissiveStrength
#define MAT_ALPHAMODE mat.alphaMode
#define MAT_HIGHLIGHT mat.highlight
#else
uniform vec4 uMaterialBaseColor;
uniform vec4 uEmissiveFactor;
uniform float uAlphaCutoff;
uniform float uMetallic;
uniform float uRoughness;
uniform float uNormalScale;
uniform float uOcclusionStrength;
uniform float uEmissiveStrength;
uniform int uAlphaMode;
uniform int uHighlight;    // 选中高亮标志（任务 2.3）
uniform sampler2D baseColorTexture;
uniform sampler2D metallicRoughnessTexture;
uniform sampler2D normalTexture;
uniform sampler2D occlusionTexture;
uniform sampler2D emissiveTexture;
#define MAT_BASECOLOR uMaterialBaseColor
#define MAT_EMISSIVE uEmissiveFactor
#define MAT_ALPHACUTOFF uAlphaCutoff
#define MAT_METALLIC uMetallic
#define MAT_ROUGHNESS uRoughness
#define MAT_NORMALSCALE uNormalScale
#define MAT_OCCLUSIONSTRENGTH uOcclusionStrength
#define MAT_EMISSIVESTRENGTH uEmissiveStrength
#define MAT_ALPHAMODE uAlphaMode
#define MAT_HIGHLIGHT uHighlight
#endif

layout(location = 0) in vec3 fragViewPosition;
layout(location = 1) in vec3 fragViewNormal;
layout(location = 2) in vec3 fragWorldPosition;
layout(location = 3) in vec3 fragObjectPosition;
layout(location = 4) in vec3 fragObjectNormal;
layout(location = 5) in vec3 fragColor;
layout(location = 6) in vec2 fragTexCoord; // 所有 PBR 贴图共用第一套 UV（1.5）
layout(location = 7) in vec4 fragObjectTangent; // xyz 切线 + w 手性（1.5）

layout(location = 0) out vec4 outColor;

// 光照常量（简化模型：单方向太阳光 + 半球环境光；完整 IBL 属 1.7 之后）。
const float kPi = 3.14159265359;
const vec3 kSunColor = vec3(1.0, 0.98, 0.92);   // 线性太阳色
const float kSunIntensity = 3.0;                // 太阳辐射强度（线性）

vec3 DyeColor() {
    // 高度取归一化场景空间（Z-up）的 z 分量，与显示朝向/轨道相机无关。
    float t = clamp(fragObjectPosition.z * 0.5 + 0.5, 0.0, 1.0);
    t = pow(t, 0.5);
    vec3 darkGreen = vec3(0.05, 0.35, 0.05);
    vec3 grassGreen = vec3(0.15, 0.65, 0.10);
    vec3 brown = vec3(0.70, 0.50, 0.15);
    vec3 white = vec3(0.95, 0.95, 0.85);
    vec3 color = mix(darkGreen, grassGreen, smoothstep(0.0, 0.35, t));
    color = mix(color, brown, smoothstep(0.35, 0.65, t));
    return mix(color, white, smoothstep(0.65, 1.0, t));
}

bool WireframeEnabled() {
    return ubo.shadowOptions.z > 0.5;
}

// 着色法线（归一化场景空间）：优先用插值顶点法线，退化时用屏幕导数几何法线。
vec3 ObjectNormal() {
    vec3 objNormal = vec3(0.0);
    float objNormalLengthSquared = dot(fragObjectNormal, fragObjectNormal);
    if (objNormalLengthSquared > 1.0e-12) {
        objNormal = fragObjectNormal * inversesqrt(objNormalLengthSquared);
    }

    if (WireframeEnabled()) {
        return objNormalLengthSquared > 1.0e-12 ? objNormal : vec3(0.0, 0.0, 1.0);
    }

    // Vulkan 窗口 Y 向下、OpenGL Y 向上，dFdy 符号相反（见 shadowOptions.w）。
    float screenYSign = ubo.shadowOptions.w > 0.5 ? -1.0 : 1.0;
    vec3 dpdx = dFdx(fragObjectPosition);
    vec3 dpdy = dFdy(fragObjectPosition) * screenYSign;
    vec3 geometricCross = cross(dpdx, dpdy);
    float geometricLengthSquared = dot(geometricCross, geometricCross);

    if (geometricLengthSquared <= 1.0e-20) {
        return objNormalLengthSquared > 1.0e-12 ? objNormal : vec3(0.0, 0.0, 1.0);
    }
    vec3 geometricNormal = geometricCross * inversesqrt(geometricLengthSquared);
    if (objNormalLengthSquared <= 1.0e-12) {
        return geometricNormal;
    }
    if (dot(objNormal, geometricNormal) < 0.0) {
        objNormal = -objNormal;
    }
    return normalize(mix(geometricNormal, objNormal, 0.58));
}

// 视空间着色法线（灰度/染色模式沿用旧路径）。
vec3 ShadedViewNormal() {
    vec3 viewDirection = normalize(-fragViewPosition);

    vec3 objNormal = vec3(0.0);
    float objNormalLengthSquared = dot(fragViewNormal, fragViewNormal);
    if (objNormalLengthSquared > 1.0e-12) {
        objNormal = fragViewNormal * inversesqrt(objNormalLengthSquared);
        if (dot(objNormal, viewDirection) < 0.0) {
            objNormal = -objNormal;
        }
    }

    if (WireframeEnabled()) {
        if (objNormalLengthSquared > 1.0e-12) {
            return objNormal;
        }
        return vec3(0.0, 0.0, 1.0);
    }

    vec3 dpdx = dFdx(fragViewPosition);
    vec3 dpdy = dFdy(fragViewPosition);
    vec3 geometricCross = cross(dpdx, dpdy);
    float geometricLengthSquared = dot(geometricCross, geometricCross);

    if (geometricLengthSquared <= 1.0e-20) {
        if (objNormalLengthSquared > 1.0e-12) {
            return objNormal;
        }
        return vec3(0.0, 0.0, 1.0);
    }

    vec3 geometricNormal = geometricCross * inversesqrt(geometricLengthSquared);
    if (dot(geometricNormal, viewDirection) < 0.0) {
        geometricNormal = -geometricNormal;
    }
    if (objNormalLengthSquared <= 1.0e-12) {
        return geometricNormal;
    }
    if (dot(objNormal, geometricNormal) < 0.0) {
        objNormal = -objNormal;
    }
    return normalize(mix(geometricNormal, objNormal, 0.58));
}

float ArtisticGray(vec3 normal) {
    vec3 viewDirection = normalize(-fragViewPosition);
    vec3 keyLight = normalize(vec3(-0.50, 0.66, 0.56));
    float facing = clamp(dot(normal, viewDirection), 0.0, 1.0);
    float keyDiffuse = max(dot(normal, keyLight), 0.0);
    float broadLighting = 0.45 * facing + 0.55 * keyDiffuse;
    float recess = pow(1.0 - facing, 1.15);
    float directionalHighlight = pow(keyDiffuse, 3.5);
    float frontFill = pow(facing, 4.0);
    float gray = 0.02
               + 0.31 * pow(max(broadLighting, 0.0), 0.90)
               + 0.22 * directionalHighlight
               + 0.40 * frontFill
               - 0.25 * recess;
    gray = clamp(gray, 0.025, 0.88);
    return pow(gray, 0.92);
}

float ShadowFactor() {
    if (ubo.shadowOptions.x < 0.5) {
        return 1.0;
    }

    // 法线偏移（1.7）：沿归一化场景空间法线偏移若干世界纹素，缓解斜面上的阴影失真。
    vec3 shadingNormal = ObjectNormal();
    vec4 lightClip = ubo.lightViewProj *
        vec4(fragObjectPosition + shadingNormal * ubo.shadowParams.z, 1.0);
    if (lightClip.w <= 1.0e-6) {
        return 1.0;
    }

    vec3 ndc = lightClip.xyz / lightClip.w;
    vec2 uv = ndc.xy * 0.5 + 0.5;
    float depth = mix(ndc.z, ndc.z * 0.5 + 0.5, ubo.shadowOptions.y);
    if (uv.x < 0.0 || uv.x > 1.0 || uv.y < 0.0 || uv.y > 1.0 ||
        depth < 0.0 || depth > 1.0) {
        return 1.0;
    }

    // 斜率缩放偏移（1.7）：N·L 越小（斜射）偏移越大，抑制 Shadow Acne。
    vec3 L = normalize(ubo.sunDirection.xyz);
    float NdotL = max(dot(shadingNormal, L), 0.0);
    float slope = sqrt(max(1.0 - NdotL * NdotL, 0.0)) / max(NdotL, 0.1);
    float bias = ubo.shadowParams.x * (1.0 + clamp(slope, 0.0, 4.0));
    float ref = depth - bias;

    vec2 texel = 1.0 / vec2(textureSize(shadowMap, 0));
    int pcf = int(ubo.shadowParams.y + 0.5);
    float shadow = 0.0;
    if (pcf <= 0) {
        // 关闭 PCF：单次比较（硬边）。
        shadow = texture(shadowMap, vec3(uv, ref));
    } else if (pcf == 1) {
        // 3x3 PCF。
        for (int x = -1; x <= 1; ++x) {
            for (int y = -1; y <= 1; ++y) {
                shadow += texture(shadowMap, vec3(uv + vec2(float(x), float(y)) * texel, ref));
            }
        }
        shadow /= 9.0;
    } else {
        // 5x5 PCF（固定偏移采样）。
        for (int x = -2; x <= 2; ++x) {
            for (int y = -2; y <= 2; ++y) {
                shadow += texture(shadowMap, vec3(uv + vec2(float(x), float(y)) * texel, ref));
            }
        }
        shadow /= 25.0;
    }
    return shadow;
}

// 灰度/染色模式使用的太阳因子（Lambert × 阴影，含夜间环境光）。
float SunLighting() {
    vec3 sun = normalize(ubo.sunDirection.xyz);
    vec3 normal = ObjectNormal();
    float lambert = max(dot(normal, sun), 0.0);
    if (ubo.displayOptions.w < 0.5) {
        return 0.03;
    }
    return 0.04 + 0.96 * lambert * ShadowFactor();
}

// ---------------- Cook-Torrance BRDF（1.5） ----------------

// GGX / Trowbridge-Reitz 法线分布项。
float DistributionGGX(float NdotH, float roughness) {
    float a = roughness * roughness;
    float a2 = a * a;
    float denom = NdotH * NdotH * (a2 - 1.0) + 1.0;
    return a2 / max(kPi * denom * denom, 1.0e-7);
}

// Smith 几何遮蔽项（Schlick-GGX，直接光 k = (r+1)^2 / 8）。
float GeometrySmith(float NdotV, float NdotL, float roughness) {
    float r = roughness + 1.0;
    float k = (r * r) / 8.0;
    float ggxV = NdotV / (NdotV * (1.0 - k) + k);
    float ggxL = NdotL / (NdotL * (1.0 - k) + k);
    return ggxV * ggxL;
}

// Schlick Fresnel 近似。
vec3 FresnelSchlick(float cosTheta, vec3 F0) {
    return F0 + (1.0 - F0) * pow(clamp(1.0 - cosTheta, 0.0, 1.0), 5.0);
}

// 切线空间法线贴图 -> 归一化场景空间法线（TBN 基由顶点切线构造）。
vec3 ApplyNormalMap(vec3 N, vec3 T, float tangentHand) {
    vec3 n = texture(normalTexture, fragTexCoord).xyz * 2.0 - 1.0;
    n.xy *= MAT_NORMALSCALE;
    // 切线正交化；退化时退回几何法线（默认平面法线贴图 (0.5,0.5,1) 得到 n=(0,0,1)，无扰动）。
    vec3 t = T - N * dot(N, T);
    float tLenSq = dot(t, t);
    if (tLenSq > 1.0e-12) {
        t *= inversesqrt(tLenSq);
        vec3 b = cross(N, t) * tangentHand;
        return normalize(mat3(t, b, N) * n);
    }
    return N;
}

// 单方向光 PBR 直接光照（含阴影因子）。返回线性 RGB。
vec3 ComputeDirectLighting(vec3 N, vec3 V, vec3 albedo, float metallic, float roughness) {
    vec3 L = normalize(ubo.sunDirection.xyz);
    vec3 H = normalize(V + L);
    float NdotL = max(dot(N, L), 0.0);
    float NdotV = max(dot(N, V), 1.0e-4);
    float NdotH = max(dot(N, H), 0.0);
    float VdotH = max(dot(V, H), 0.0);

    // 能量守恒：金属无漫反射，F0 = 0.04（电介质）或 albedo（金属）。
    vec3 F0 = mix(vec3(0.04), albedo, metallic);
    vec3 F = FresnelSchlick(VdotH, F0);
    vec3 kd = (1.0 - F) * (1.0 - metallic);

    float D = DistributionGGX(NdotH, roughness);
    float G = GeometrySmith(NdotV, NdotL, roughness);
    vec3 specular = (D * G * F) / max(4.0 * NdotV * NdotL, 1.0e-4);

    vec3 radiance = kSunColor * kSunIntensity * ShadowFactor();
    vec3 diffuse = kd * albedo / kPi;
    return (diffuse + specular) * radiance * NdotL;
}

// 半球环境光（1.7）：按归一化场景空间法线的上下方向在地面色与天空色之间插值。
// 归一化场景空间 +Z 为上方（与 sunDirection 约定一致）。强度由 ambientSkyColor.w 统一缩放。
vec3 ComputeAmbient(vec3 N, vec3 albedo, float metallic, float ao) {
    float up = clamp(N.z * 0.5 + 0.5, 0.0, 1.0);
    float intensity = ubo.ambientSkyColor.w;
    vec3 skyColor = ubo.ambientSkyColor.rgb * intensity;
    vec3 groundColor = ubo.ambientGroundColor.rgb * intensity;
    vec3 irradiance = mix(groundColor, skyColor, up);

    vec3 F0 = mix(vec3(0.04), albedo, metallic);
    vec3 ambientDiffuse = irradiance * albedo * (1.0 - metallic) * ao;
    vec3 ambientSpecular = irradiance * F0 * ao;
    return ambientDiffuse + ambientSpecular;
}

// Metallic-Roughness PBR（默认 3D 着色路径）。返回线性 RGB。
vec3 ComputePBR(vec3 baseColor, float metallic, float roughness, float ao) {
    vec3 N = ObjectNormal();
    vec3 T = fragObjectTangent.xyz;
    N = ApplyNormalMap(N, T, fragObjectTangent.w);
    // 视线向量：相机（归一化场景空间）到片元（归一化场景空间）。
    vec3 V = normalize(ubo.cameraWorldPosition.xyz - fragObjectPosition);

    vec3 albedo = baseColor;
    vec3 color = ComputeDirectLighting(N, V, albedo, metallic, roughness)
               + ComputeAmbient(N, albedo, metallic, ao);
    return color;
}

// 调试视图（1.7）。返回线性 RGB（[0,1] 显示值）。0 表示未启用。
vec3 DebugViewColor() {
    int mode = int(ubo.debugOptions.x + 0.5);
    if (mode == 1) {
        // 线性深度：近=黑、远=白，按近/远平面归一化（视空间）。
        float viewDepth = max(-fragViewPosition.z, 0.0);
        float nearPlane = max(ubo.depthRange.x, 1.0e-4);
        float farPlane = max(ubo.depthRange.y, nearPlane + 1.0e-4);
        float linearDepth = clamp((viewDepth - nearPlane) / (farPlane - nearPlane), 0.0, 1.0);
        return vec3(linearDepth);
    }
    if (mode == 2) {
        // 世界空间法线：法线已在归一化场景空间（着色世界），映射到 [0,1] 颜色。
        vec3 worldNormal = normalize(fragObjectNormal);
        return worldNormal * 0.5 + 0.5;
    }
    if (mode == 3) {
        // 阴影项：被照亮=白，处于阴影=黑（灰度）。
        return vec3(ShadowFactor());
    }
    return vec3(0.0);
}

void main() {
    // 颜色顺序（fill 与 wireframe 共用）：
    // 1. 采样基础色：base = material.baseColor.rgb * 顶点色 * baseColorTexture.rgb（无纹理 -> 1x1 白）。
    // 2. 采样 metallicRoughness（G=roughness，B=metallic，R=AO 兼容 glTF 合并图约定）。
    // 3. 采样 AO（R 通道）与自发光。
    // 4. 默认路径走 Cook-Torrance PBR（直接光 + 半球环境光）；
    //    染色/灰度/线框为覆盖模式，沿用旧显示逻辑。
    // 5. 光照分析开启时给 PBR 直接光叠加阴影（阴影贴图仅在光照分析模式下渲染）。
    // 6. 调试视图（1.7）开启时覆盖最终颜色（后处理会跳过曝光/色调映射）。
    const float grayEnabled = ubo.displayOptions.x;
    const float dyeEnabled = ubo.displayOptions.y;

    vec4 baseTexColor = texture(baseColorTexture, fragTexCoord);
    vec3 baseColor = MAT_BASECOLOR.rgb * fragColor * baseTexColor.rgb;
    float alpha = MAT_BASECOLOR.a * baseTexColor.a;

    if (MAT_ALPHAMODE == 1) {  // Mask
        if (alpha < MAT_ALPHACUTOFF) {
            discard;
        }
        alpha = 1.0;
    }

    vec4 mrSample = texture(metallicRoughnessTexture, fragTexCoord);
    float roughness = clamp(MAT_ROUGHNESS * mrSample.g, 0.04, 1.0);
    float metallic = clamp(MAT_METALLIC * mrSample.b, 0.0, 1.0);
    float ao = mix(1.0, texture(occlusionTexture, fragTexCoord).r, MAT_OCCLUSIONSTRENGTH);
    vec3 emissive = MAT_EMISSIVE.rgb * MAT_EMISSIVESTRENGTH *
                    texture(emissiveTexture, fragTexCoord).rgb;

    vec3 color;
    if (dyeEnabled > 0.5) {
        color = DyeColor();
        if (grayEnabled > 0.5) {
            color *= ArtisticGray(ShadedViewNormal());
        }
        if (ubo.displayOptions.z > 0.5) {
            color *= SunLighting();
        }
    } else if (grayEnabled > 0.5) {
        color = vec3(ArtisticGray(ShadedViewNormal()));
        if (ubo.displayOptions.z > 0.5) {
            color *= SunLighting();
        }
    } else {
        // 默认：Metallic-Roughness PBR。
        color = ComputePBR(baseColor, metallic, roughness, ao) + emissive;
    }

    // 调试视图覆盖最终颜色。
    if (ubo.debugOptions.x > 0.5) {
        color = DebugViewColor();
        alpha = 1.0;
    }

    // 选中高亮（任务 2.3）：对所有模式（默认 PBR / 灰度 / 染色 / 线框 / 调试视图）
    // 统一把最终颜色向橙色混合，便于定位选中对象。
    if (MAT_HIGHLIGHT != 0) {
        const vec3 kHighlightColor = vec3(1.0, 0.6, 0.1);
        color = mix(color, kHighlightColor, 0.45);
    }

    outColor = vec4(color, alpha);
}
