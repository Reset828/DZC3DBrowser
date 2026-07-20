#version 450
layout(location = 0) in vec3 fragColor;
layout(location = 1) in vec3 fragViewPosition;
layout(location = 2) flat in float grayEnabled;
layout(location = 3) in vec3 fragViewNormal;

layout(location = 0) out vec4 outColor;

void main() {
    if (grayEnabled > 0.5) {
        // The geometric normal preserves the triangular surface detail seen in
        // dense scan meshes. OBJ normals still contribute to the final normal
        // so authored shape information remains part of the rendering.
        vec3 geometricNormal = normalize(
            cross(dFdx(fragViewPosition), dFdy(fragViewPosition)));
        vec3 viewDirection = normalize(-fragViewPosition);
        if (dot(geometricNormal, viewDirection) < 0.0) {
            geometricNormal = -geometricNormal;
        }

        float objNormalLengthSquared = dot(fragViewNormal, fragViewNormal);
        vec3 normal = geometricNormal;
        if (objNormalLengthSquared > 1.0e-12) {
            vec3 objNormal = fragViewNormal * inversesqrt(objNormalLengthSquared);
            if (dot(objNormal, geometricNormal) < 0.0) {
                objNormal = -objNormal;
            }
            normal = normalize(mix(objNormal, geometricNormal, 0.72));
        }

        // A high, oblique key light exposes slopes and cavities. The weak
        // camera-side fill prevents completely black faces without flattening
        // the relief.
        vec3 mainLight = normalize(vec3(-0.58, 0.72, 0.38));
        vec3 fillLight = normalize(vec3(0.20, -0.15, 1.00));
        float mainDiffuse = max(dot(normal, mainLight), 0.0);
        float fillDiffuse = max(dot(normal, fillLight), 0.0);
        float lighting = 0.13 + 0.78 * mainDiffuse + 0.09 * fillDiffuse;

        // Expand local contrast to produce the bright facets and dark recesses
        // of the reference image while retaining continuous grayscale.
        float gray = smoothstep(0.05, 0.95, lighting);
        gray = pow(gray, 0.92);
        outColor = vec4(vec3(gray), 1.0);
    } else {
        outColor = vec4(fragColor, 1.0);
    }
}
