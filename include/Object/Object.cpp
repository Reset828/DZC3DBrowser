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
        m_localTransform = obj.m_localTransform;
        m_worldMatrix = obj.m_worldMatrix;
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

// ---------------- 局部变换与世界矩阵（任务 2.1） ----------------

// 设置局部变换并把自身与后代标记为脏。
void Object::SetLocalTransform(const Transform& transform) {
    m_localTransform = transform;
    MarkWorldTransformDirty();
}

// 返回局部变换。
const Transform& Object::GetLocalTransform() const {
    return m_localTransform;
}

// 便捷设置平移。
void Object::SetTranslation(const Vec3& translation) {
    m_localTransform.translation = translation;
    MarkWorldTransformDirty();
}

// 便捷设置旋转（度）。
void Object::SetRotationDegrees(const Vec3& rotationDegrees) {
    m_localTransform.rotationDegrees = rotationDegrees;
    MarkWorldTransformDirty();
}

// 便捷设置缩放。
void Object::SetScale(const Vec3& scale) {
    m_localTransform.scale = scale;
    MarkWorldTransformDirty();
}

// 复位为默认变换（单位）。
void Object::ResetTransform() {
    m_localTransform = Transform{};
    MarkWorldTransformDirty();
}

// 世界矩阵（局部 -> 原始模型世界；不含场景归一化）。
const Mat4& Object::GetWorldMatrix() const {
    return m_worldMatrix;
}

// 标记自身及所有后代的世界矩阵需要重算。
void Object::MarkWorldTransformDirty() {
    SetDirty(true);
}

// 自顶向下刷新世界矩阵；parentChanged 表示父链本帧已变化。
// 返回本节点世界矩阵是否发生变化。
bool Object::UpdateWorldTransforms(const Mat4* parentWorld, bool parentChanged) {
    const bool needUpdate = IsDirty() || parentChanged;
    if (!needUpdate) return false;
    const Mat4 local = m_localTransform.ToMatrix();
    m_worldMatrix = parentWorld ? TransformMultiply(*parentWorld, local) : local;
    SetDirty(false);
    return true;
}
