#ifndef __SCENE_OBJECT_H__
#define __SCENE_OBJECT_H__

#include <cstdint>

#ifndef __ENGINE_VEC_TYPES_H__
#define __ENGINE_VEC_TYPES_H__
struct Vec2 { float x, y; };
struct Vec3 { float x, y, z; };
struct Vec4 { float x, y, z, w; };
struct Mat4 { float m[4][4]; };
struct Rect2D { int x, y, width, height; };
#endif

class SceneObject {
public:
    SceneObject();
    SceneObject(const SceneObject& obj);
    virtual ~SceneObject();

    SceneObject& operator=(const SceneObject& obj);

    enum ObjectType {
        OT_OBJECT = 0,
        OT_LINE,
        OT_LAYER
    };

    enum FlagType {
        FT_VISIBLE = 1,
        FT_DIRTY = 16,
    };

    void SetParent(SceneObject* pParent);
    SceneObject* GetParent() const;

    void SetDirty(bool bDirty);
    bool IsDirty() const;

    uint32_t GetType() const;

    void SetVisible(bool bVisible);
    bool IsVisible() const;

    void SetColor(const Vec4& clr);
    void SetColor(float r, float g, float b, float a);
    Vec4 GetColor() const;

    virtual void Render(int iMode = 0) = 0;

protected:
    uint8_t m_uType = OT_OBJECT;
    SceneObject* m_pParent = nullptr;
    uint8_t m_uFlag = FT_VISIBLE;
    uint32_t m_uClr = 0xFFFFFFFF;

private:
    void EnableFlag(FlagType ft, bool bEnable);
    bool IsFlagEnabled(FlagType ft) const;
};

#endif //__SCENE_OBJECT_H__
