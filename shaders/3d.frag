#version 450
layout(location = 0) in vec3 fragColor;
layout(location = 1) in vec3 fragViewPosition;
layout(location = 2) flat in float grayEnabled;
layout(location = 0) out vec4 outColor;
void main() {
    if (grayEnabled > 0.5) {
        // 从片元位置导数重建三角面法线，不要求 OBJ 携带法线数据。
        vec3 normal = normalize(cross(dFdx(fragViewPosition), dFdy(fragViewPosition)));
        if (!gl_FrontFacing) {
            normal = -normal;
        }

        // 左上方主光源配合柔和的正面补光，形成参考图中的岩面灰度。
        vec3 mainLight = normalize(vec3(-0.45, 0.65, 0.60));
        vec3 fillLight = normalize(vec3(0.35, -0.20, 0.75));
        float mainDiffuse = max(dot(normal, mainLight), 0.0);
        float fillDiffuse = max(dot(normal, fillLight), 0.0);
        float lighting = clamp(0.26 + 0.66 * mainDiffuse + 0.18 * fillDiffuse,
                               0.0, 1.0);

        // 稍微抬高中间调，同时保留沟槽和背光面的暗部层次。
        float gray = pow(lighting, 0.85);
        outColor = vec4(vec3(gray), 1.0);
    } else {
        outColor = vec4(fragColor, 1.0);
    }
}
