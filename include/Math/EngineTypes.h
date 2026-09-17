#ifndef __ENGINE_TYPES_H__
#define __ENGINE_TYPES_H__

struct Vec2 { float x, y; };
struct Vec3 { float x, y, z; };
struct Vec4 { float x, y, z, w; };
struct Mat4 { float m[4][4]; };
struct Rect2D { int x, y, width, height; };
struct Aabb {
    Vec3 min = { 0.0f, 0.0f, 0.0f };
    Vec3 max = { 0.0f, 0.0f, 0.0f };
};

#endif //__ENGINE_TYPES_H__
