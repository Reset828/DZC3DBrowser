#version 450
layout(location = 0) in vec3 fragColor;
layout(location = 1) in vec3 fragViewPosition;
layout(location = 2) flat in float grayEnabled;
layout(location = 3) in vec3 fragViewNormal;

layout(location = 0) out vec4 outColor;

void main() {
    if (grayEnabled > 0.5) {
        // 优先使用 OBJ 法线；缺失时由片元位置导数重建三角面法线。
        // 后一种方式无需按面复制顶点，同时天然保持硬边。
        float normalLengthSquared = dot(fragViewNormal, fragViewNormal);
        vec3 normal = normalLengthSquared > 1.0e-12
            ? fragViewNormal * inversesqrt(normalLengthSquared)
            : normalize(cross(dFdx(fragViewPosition), dFdy(fragViewPosition)));
        if (!gl_FrontFacing) {
            normal = -normal;
        }

        vec3 mainLight = normalize(vec3(-0.45, 0.65, 0.60));
        vec3 fillLight = normalize(vec3(0.35, -0.20, 0.75));
        float mainDiffuse = max(dot(normal, mainLight), 0.0);
        float fillDiffuse = max(dot(normal, fillLight), 0.0);
        float lighting = clamp(0.26 + 0.66 * mainDiffuse + 0.18 * fillDiffuse,
                               0.0, 1.0);

        float gray = pow(lighting, 0.85);
        outColor = vec4(vec3(gray), 1.0);
    } else {
        outColor = vec4(fragColor, 1.0);
    }
}
