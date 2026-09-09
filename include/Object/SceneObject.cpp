#include "SceneObject.h"

SceneObject::SceneObject() {}

SceneObject::SceneObject(const SceneObject& obj) {
    *this = obj;
}

SceneObject::~SceneObject() {}

SceneObject& SceneObject::operator=(const SceneObject& obj) {
    if (this != &obj) {
        m_pParent = obj.m_pParent;
        m_uType = obj.m_uType;
        m_uFlag = obj.m_uFlag;
        m_uClr = obj.m_uClr;
    }
    return *this;
}

void SceneObject::SetParent(SceneObject* pParent) {
    m_pParent = pParent;
}

SceneObject* SceneObject::GetParent() const {
    return m_pParent;
}

void SceneObject::SetDirty(bool bDirty) {
    EnableFlag(FT_DIRTY, bDirty);
}

bool SceneObject::IsDirty() const {
    return IsFlagEnabled(FT_DIRTY);
}

uint32_t SceneObject::GetType() const {
    return m_uType;
}

void SceneObject::SetVisible(bool bVisible) {
    EnableFlag(FT_VISIBLE, bVisible);
}

bool SceneObject::IsVisible() const {
    return IsFlagEnabled(FT_VISIBLE);
}

void SceneObject::SetColor(const Vec4& clr) {
    uint8_t r = static_cast<uint8_t>(clr.x * 255.0f);
    uint8_t g = static_cast<uint8_t>(clr.y * 255.0f);
    uint8_t b = static_cast<uint8_t>(clr.z * 255.0f);
    uint8_t a = static_cast<uint8_t>(clr.w * 255.0f);
    m_uClr = (static_cast<uint32_t>(r)) |
             (static_cast<uint32_t>(g) << 8) |
             (static_cast<uint32_t>(b) << 16) |
             (static_cast<uint32_t>(a) << 24);
}

void SceneObject::SetColor(float r, float g, float b, float a) {
    SetColor(Vec4{ r, g, b, a });
}

Vec4 SceneObject::GetColor() const {
    float r = static_cast<float>((m_uClr >> 0) & 0xFF) / 255.0f;
    float g = static_cast<float>((m_uClr >> 8) & 0xFF) / 255.0f;
    float b = static_cast<float>((m_uClr >> 16) & 0xFF) / 255.0f;
    float a = static_cast<float>((m_uClr >> 24) & 0xFF) / 255.0f;
    return Vec4{ r, g, b, a };
}

void SceneObject::EnableFlag(FlagType ft, bool bEnable) {
    if (bEnable) {
        m_uFlag |= static_cast<uint8_t>(ft);
    } else {
        m_uFlag &= ~static_cast<uint8_t>(ft);
    }
}

bool SceneObject::IsFlagEnabled(FlagType ft) const {
    return (m_uFlag & static_cast<uint8_t>(ft)) != 0;
}
