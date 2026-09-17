#ifndef __ASSET_VERTEX_H__
#define __ASSET_VERTEX_H__

// 与具体渲染后端解耦的顶点：位置、法线、纹理坐标、顶点色。
struct AssetVertex {
    float position[3] = { 0.0f, 0.0f, 0.0f };
    float normal[3] = { 0.0f, 0.0f, 0.0f };
    float texCoord[2] = { 0.0f, 0.0f };
    float color[3] = { 1.0f, 1.0f, 1.0f };
};

#endif //__ASSET_VERTEX_H__
