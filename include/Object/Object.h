#ifndef __OBJECT_H__
#define __OBJECT_H__

#include <cstdint>
#include "Math/EngineTypes.h"


/**********
    Object                 场景节点：可见性 / 颜色 / 父子
      |
      +-- Layer            子对象容器，遍历 Render
      +-- GLObject         OpenGL VAO / VBO / EBO
      |     \-- GLMesh
      +-- VKObject         Vulkan vertex / index buffer
            \-- VKMesh
*************/
class Object {
public:
    Object();
    Object(const Object& obj);
    virtual ~Object();

// operator=：复制对象的公共状态。
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

    // 设置父对象。
    void SetParent(Object* pParent);
    // 获取父对象。
    Object* GetParent() const;

    // 设置脏标记。
    void SetDirty(bool bDirty);
    // 查询脏标记。
    bool IsDirty() const;

    // 返回对象类型。
    uint32_t GetType() const;

    // 设置对象可见性。
    void SetVisible(bool bVisible);
    // 查询对象可见性。
    bool IsVisible() const;

    // 设置对象颜色。
    void SetColor(const Vec4& clr);
    // 设置对象颜色。
    void SetColor(float r, float g, float b, float a);
    // 获取对象颜色。
    Vec4 GetColor() const;

    // 绘制自身；Layer 则遍历子对象。
    virtual void Render(int iMode = 0) = 0;

protected:
    uint8_t m_uType = OT_OBJECT;
    Object* m_pParent = nullptr;
    uint8_t m_uFlag = FT_VISIBLE;
    uint32_t m_uClr = 0xFFFFFFFF;

private:
    // 打开或关闭指定标志位。
    void EnableFlag(FlagType ft, bool bEnable);
    // 查询对象标志位。
    bool IsFlagEnabled(FlagType ft) const;
};

#endif //__OBJECT_H__
