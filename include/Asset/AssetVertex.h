#ifndef __ASSET_VERTEX_H__
#define __ASSET_VERTEX_H__

// 与具体渲染后端解耦的顶点：位置、法线、纹理坐标、顶点色、切线。
// 切线用于法线贴图的 TBN 基（1.5）；w 分量为副切线手性符号（bitangent sign）。
struct AssetVertex {
    float position[3] = { 0.0f, 0.0f, 0.0f };
    float normal[3] = { 0.0f, 0.0f, 0.0f };
    float texCoord[2] = { 0.0f, 0.0f };
    float color[3] = { 1.0f, 1.0f, 1.0f };
    // xyz = 切线方向（与法线正交，沿 +U 方向），w = 手性（+1/-1）；默认 (1,0,0,1)。
    float tangent[4] = { 1.0f, 0.0f, 0.0f, 1.0f };
};

#endif //__ASSET_VERTEX_H__
