#version 450
layout(location = 0) in vec3 fragColor;
layout(location = 1) in vec3 fragViewPosition;
layout(location = 2) flat in float grayEnabled;
layout(location = 3) in vec3 fragViewNormal;
layout(location = 4) in vec3 fragWorldPosition;
layout(location = 5) flat in float dyeEnabled;
layout(location = 6) flat in float lightAnalysisEnabled;
layout(location = 7) flat in float sunAboveHorizon;
layout(location = 8) flat in vec3 fragSunDirection;
layout(location = 9) in vec3 fragObjectPosition;
layout(location = 10) in vec3 fragObjectNormal;

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

vec3 ShadedViewNormal() {
    vec3 geometricNormal = normalize(
        cross(dFdx(fragViewPosition), dFdy(fragViewPosition)));
    vec3 viewDirection = normalize(-fragViewPosition);
    if (dot(geometricNormal, viewDirection) < 0.0) {
        geometricNormal = -geometricNormal;
    }
    vec3 normal = geometricNormal;
    float objNormalLengthSquared = dot(fragViewNormal, fragViewNormal);
    if (objNormalLengthSquared > 1.0e-12) {
        vec3 objNormal = fragViewNormal * inversesqrt(objNormalLengthSquared);
        if (dot(objNormal, geometricNormal) < 0.0) {
            objNormal = -objNormal;
        }
        normal = normalize(mix(geometricNormal, objNormal, 0.58));
    }
    return normal;
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
    vec3 geometricNormal = normalize(
        cross(dFdx(fragObjectPosition), dFdy(fragObjectPosition)));
    vec3 normal = geometricNormal;
    float objNormalLengthSquared = dot(fragObjectNormal, fragObjectNormal);
    if (objNormalLengthSquared > 1.0e-12) {
        vec3 objNormal = fragObjectNormal * inversesqrt(objNormalLengthSquared);
        if (dot(objNormal, geometricNormal) < 0.0) {
            objNormal = -objNormal;
        }
        normal = normalize(mix(geometricNormal, objNormal, 0.58));
    }
    float sunLengthSquared = dot(fragSunDirection, fragSunDirection);
    if (sunLengthSquared <= 1.0e-12) {
        return 0.0;
    }
    vec3 sun = fragSunDirection * inversesqrt(sunLengthSquared);
    return max(dot(normal, sun), 0.0);
}

float SunLighting() {
    if (sunAboveHorizon < 0.5) {
        return 0.03;
    }
    return 0.04 + 0.96 * SunLambert();
}

void main() {
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
        color = fragColor;
    }

    if (lightAnalysisEnabled > 0.5) {
        color *= SunLighting();
    }

    outColor = vec4(color, 1.0);
}
