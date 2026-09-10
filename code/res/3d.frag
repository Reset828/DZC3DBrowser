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

layout(binding = 1) uniform sampler2DShadow shadowMap;

layout(location = 0) in vec3 fragViewPosition;
layout(location = 1) in vec3 fragViewNormal;
layout(location = 2) in vec3 fragWorldPosition;
layout(location = 3) in vec3 fragObjectPosition;
layout(location = 4) in vec3 fragObjectNormal;

layout(location = 0) out vec4 outColor;

vec3 DyeColor() {
    float t = clamp(fragWorldPosition.z * 0.5 + 0.5, 0.0, 1.0);
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

    // GL_LINE / VK_POLYGON_MODE_LINE fragments have degenerate screen
    // derivatives, so gray would otherwise keep the lighting color.
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

float SunLambert() {
    float sunLengthSquared = dot(ubo.sunDirection.xyz, ubo.sunDirection.xyz);
    if (sunLengthSquared <= 1.0e-12) {
        return 0.0;
    }
    vec3 sun = ubo.sunDirection.xyz * inversesqrt(sunLengthSquared);

    vec3 objNormal = vec3(0.0);
    float objNormalLengthSquared = dot(fragObjectNormal, fragObjectNormal);
    if (objNormalLengthSquared > 1.0e-12) {
        objNormal = fragObjectNormal * inversesqrt(objNormalLengthSquared);
    }

    vec3 normal;
    if (WireframeEnabled()) {
        if (objNormalLengthSquared <= 1.0e-12) {
            return 0.0;
        }
        normal = objNormal;
    } else {
        vec3 dpdx = dFdx(fragObjectPosition);
        vec3 dpdy = dFdy(fragObjectPosition);
        vec3 geometricCross = cross(dpdx, dpdy);
        float geometricLengthSquared = dot(geometricCross, geometricCross);

        if (geometricLengthSquared <= 1.0e-20) {
            if (objNormalLengthSquared > 1.0e-12) {
                normal = objNormal;
            } else {
                return 0.0;
            }
        } else {
            vec3 geometricNormal = geometricCross * inversesqrt(geometricLengthSquared);
            if (objNormalLengthSquared <= 1.0e-12) {
                normal = geometricNormal;
            } else {
                if (dot(objNormal, geometricNormal) < 0.0) {
                    objNormal = -objNormal;
                }
                normal = normalize(mix(geometricNormal, objNormal, 0.58));
            }
        }
    }
    return max(dot(normal, sun), 0.0);
}

float ShadowFactor() {
    if (ubo.shadowOptions.x < 0.5) {
        return 1.0;
    }

    vec4 lightClip = ubo.lightViewProj * vec4(fragObjectPosition, 1.0);
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

    const float bias = 0.002;
    float ref = depth - bias;
    vec2 texel = 1.0 / vec2(textureSize(shadowMap, 0));
    float shadow = 0.0;
    for (int x = -1; x <= 1; ++x) {
        for (int y = -1; y <= 1; ++y) {
            shadow += texture(shadowMap, vec3(uv + vec2(float(x), float(y)) * texel, ref));
        }
    }
    return shadow / 9.0;
}

float SunLighting() {
    if (ubo.displayOptions.w < 0.5) {
        return 0.03;
    }
    return 0.04 + 0.96 * SunLambert() * ShadowFactor();
}

void main() {
    const float grayEnabled = ubo.displayOptions.x;
    const float dyeEnabled = ubo.displayOptions.y;
    const float lightAnalysisEnabled = ubo.displayOptions.z;

    vec3 color;
    if (dyeEnabled > 0.5) {
        color = DyeColor();
        if (grayEnabled > 0.5) {
            color *= ArtisticGray(ShadedViewNormal());
        }
    } else if (grayEnabled > 0.5) {
        color = vec3(ArtisticGray(ShadedViewNormal()));
    } else if (lightAnalysisEnabled > 0.5) {
        color = vec3(0.75);
    } else {
        color = vec3(0.0);
    }

    if (lightAnalysisEnabled > 0.5) {
        color *= SunLighting();
    }

    outColor = vec4(color, 1.0);
}
