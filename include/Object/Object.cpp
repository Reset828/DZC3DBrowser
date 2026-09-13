#include "Object.h"

Object::Object() {}

Object::Object(const Object& obj) {
    *this = obj;
}

Object::~Object() {}

Object& Object::operator=(const Object& obj) {
    if (this != &obj) {
        m_pParent = obj.m_pParent;
        m_uType = obj.m_uType;
        m_uFlag = obj.m_uFlag;
        m_uClr = obj.m_uClr;
    }
    return *this;
}

// 设置父对象。
void Object::SetParent(Object* pParent) {
    m_pParent = pParent;
}

// 返回父对象指针。
Object* Object::GetParent() const {
    return m_pParent;
}

// 设置脏标记。
void Object::SetDirty(bool bDirty) {
    EnableFlag(FT_DIRTY, bDirty);
}

// 查询脏标记。
bool Object::IsDirty() const {
    return IsFlagEnabled(FT_DIRTY);
}

// 返回对象类型。
uint32_t Object::GetType() const {
    return m_uType;
}

// 设置对象可见性。
void Object::SetVisible(bool bVisible) {
    EnableFlag(FT_VISIBLE, bVisible);
}

// 查询对象是否可见。
bool Object::IsVisible() const {
    return IsFlagEnabled(FT_VISIBLE);
}

// 设置对象颜色。
void Object::SetColor(const Vec4& clr) {
    uint8_t r = static_cast<uint8_t>(clr.x * 255.0f);
    uint8_t g = static_cast<uint8_t>(clr.y * 255.0f);
    uint8_t b = static_cast<uint8_t>(clr.z * 255.0f);
    uint8_t a = static_cast<uint8_t>(clr.w * 255.0f);
    m_uClr = (static_cast<uint32_t>(r)) |
             (static_cast<uint32_t>(g) << 8) |
             (static_cast<uint32_t>(b) << 16) |
             (static_cast<uint32_t>(a) << 24);
}

// 设置对象颜色。
void Object::SetColor(float r, float g, float b, float a) {
    SetColor(Vec4{ r, g, b, a });
}

// 返回对象颜色。
Vec4 Object::GetColor() const {
    float r = static_cast<float>((m_uClr >> 0) & 0xFF) / 255.0f;
    float g = static_cast<float>((m_uClr >> 8) & 0xFF) / 255.0f;
    float b = static_cast<float>((m_uClr >> 16) & 0xFF) / 255.0f;
    float a = static_cast<float>((m_uClr >> 24) & 0xFF) / 255.0f;
    return Vec4{ r, g, b, a };
}

// 打开或关闭指定标志位。
void Object::EnableFlag(FlagType ft, bool bEnable) {
    if (bEnable) {
        m_uFlag |= static_cast<uint8_t>(ft);
    } else {
        m_uFlag &= ~static_cast<uint8_t>(ft);
    }
}

// 查询指定标志是否启用。
bool Object::IsFlagEnabled(FlagType ft) const {
    return (m_uFlag & static_cast<uint8_t>(ft)) != 0;
}
