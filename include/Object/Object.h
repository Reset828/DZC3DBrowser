#ifndef __OBJECT_H__
#define __OBJECT_H__

#include <cstdint>
#include "Math/EngineTypes.h"


class Object {
public:
    Object();
    Object(const Object& obj);
    virtual ~Object();

    Object& operator=(const Object& obj);

    enum ObjectType {
        OT_OBJECT = 0,
        OT_LINE,
        OT_LAYER
    };

    enum RenderMode {
        RM_DEFAULT = 0,
        RM_SHADOW = 1
    };

    enum FlagType {
        FT_VISIBLE = 1,
        FT_DIRTY = 16,
    };

    void SetParent(Object* pParent);
    Object* GetParent() const;

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
    Object* m_pParent = nullptr;
    uint8_t m_uFlag = FT_VISIBLE;
    uint32_t m_uClr = 0xFFFFFFFF;

private:
    void EnableFlag(FlagType ft, bool bEnable);
    bool IsFlagEnabled(FlagType ft) const;
};

#endif //__OBJECT_H__
