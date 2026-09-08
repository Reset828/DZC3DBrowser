#version 450
layout(location = 0) in vec3 fragColor;
layout(location = 1) in vec3 fragViewPosition;
layout(location = 2) flat in float grayEnabled;
layout(location = 3) in vec3 fragViewNormal;
layout(location = 4) in vec3 fragWorldPosition;
layout(location = 5) flat in float dyeEnabled;

layout(location = 0) out vec4 outColor;

void main() {
    if (dyeEnabled > 0.5) {
        float t = clamp(fragWorldPosition.z * 0.5 + 0.5, 0.0, 1.0);
        t = pow(t, 0.5);
        vec3 darkGreen = vec3(0.05, 0.35, 0.05);
        vec3 grassGreen = vec3(0.15, 0.65, 0.10);
        vec3 brown = vec3(0.70, 0.50, 0.15);
        vec3 white = vec3(0.95, 0.95, 0.85);
        vec3 color = mix(darkGreen, grassGreen, smoothstep(0.0, 0.35, t));
        color = mix(color, brown, smoothstep(0.35, 0.65, t));
        color = mix(color, white, smoothstep(0.65, 1.0, t));
        if (grayEnabled > 0.5) {
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
            gray = pow(gray, 0.92);
            color *= gray;
        }
        outColor = vec4(color, 1.0);
    } else if (grayEnabled > 0.5) {
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
        gray = pow(gray, 0.92);
        outColor = vec4(vec3(gray), 1.0);
    } else {
        outColor = vec4(fragColor, 1.0);
    }
}
