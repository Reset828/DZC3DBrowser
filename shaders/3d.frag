#version 450
layout(location = 0) in vec3 fragColor;
layout(location = 1) in vec3 fragViewPosition;
layout(location = 2) flat in float grayEnabled;
layout(location = 3) in vec3 fragViewNormal;

layout(location = 0) out vec4 outColor;

void main() {
    if (grayEnabled > 0.5) {
        vec3 geometricNormal = normalize(
            cross(dFdx(fragViewPosition), dFdy(fragViewPosition)));
        vec3 viewDirection = normalize(-fragViewPosition);
        if (dot(geometricNormal, viewDirection) < 0.0) {
            geometricNormal = -geometricNormal;
        }

        // OBJ 法线保留整体起伏，几何法线补充细小沟槽。
        vec3 normal = geometricNormal;
        float objNormalLengthSquared = dot(fragViewNormal, fragViewNormal);
        if (objNormalLengthSquared > 1.0e-12) {
            vec3 objNormal = fragViewNormal * inversesqrt(objNormalLengthSquared);
            if (dot(objNormal, geometricNormal) < 0.0) {
                objNormal = -objNormal;
            }
            normal = normalize(mix(geometricNormal, objNormal, 0.58));
        }

        // 正面宽光提亮大面，斜向光保留岩面起伏。
        vec3 keyLight = normalize(vec3(-0.50, 0.66, 0.56));
        float facing = clamp(dot(normal, viewDirection), 0.0, 1.0);
        float keyDiffuse = max(dot(normal, keyLight), 0.0);
        float broadLighting = 0.45 * facing + 0.55 * keyDiffuse;

        // 提亮正对观察者的大面，同时持续压暗沟槽和陡坡。
        float recess = pow(1.0 - facing, 1.15);
        float directionalHighlight = pow(keyDiffuse, 3.5);
        float frontFill = pow(facing, 4.0);
        float gray = 0.02
                   + 0.31 * pow(max(broadLighting, 0.0), 0.90)
                   + 0.22 * directionalHighlight
                   + 0.40 * frontFill
                   - 0.25 * recess;
        gray = clamp(gray, 0.025, 0.88);

        // 保留中间灰度，避免细密三角面被压成纯黑或纯白。
        gray = pow(gray, 0.92);
        outColor = vec4(vec3(gray), 1.0);
    } else {
        outColor = vec4(fragColor, 1.0);
    }
}
