#ifndef __OBJECT_H__
#define __OBJECT_H__

#include <cstdint>
#include "Math/EngineTypes.h"


/*******************************************************
    Object                 场景节点：可见性 / 颜色 / 父子
      |
      +-- Layer            子对象容器，遍历 Render
      +-- GLObject         OpenGL VAO / VBO / EBO
      |     \-- GLMesh
      +-- VKObject         Vulkan vertex / index buffer
            \-- VKMesh
********************************************************/
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
        RM_SHADOW = 1,
        RM_TRANSPARENT = 2  // 透明通道：只画 Blend SubMesh，由渲染器排序后调用
    };

    enum FlagType {
        FT_VISIBLE = 1,
        FT_DIRTY = 16,
    };

    // 设置父对象。
    void SetParent(Object* pParent);
    // 返回父对象指针。
    Object* GetParent() const;

    // 设置脏标记。
    void SetDirty(bool bDirty);
    // 查询脏标记。
    bool IsDirty() const;

    // 返回对象类型。
    uint32_t GetType() const;

    // 设置对象可见性。
    void SetVisible(bool bVisible);
    // 查询对象是否可见。
    bool IsVisible() const;

    // 设置对象颜色。
    void SetColor(const Vec4& clr);
    // 设置对象颜色。
    void SetColor(float r, float g, float b, float a);
    // 返回对象颜色。
    Vec4 GetColor() const;

    // 绘制自身。
    virtual void Render(int iMode = 0) = 0;

    // 只绘制指定的 SubMesh（透明排序 flush 时逐个调用）。默认空实现。
    virtual void DrawSubMesh(int subMeshIndex, int iMode = RM_DEFAULT) {
        (void)subMeshIndex;
        (void)iMode;
    }

protected:
    uint8_t m_uType = OT_OBJECT;
    Object* m_pParent = nullptr;
    uint8_t m_uFlag = FT_VISIBLE;
    uint32_t m_uClr = 0xFFFFFFFF;

private:
    // 打开或关闭指定标志位。
    void EnableFlag(FlagType ft, bool bEnable);
    // 查询指定标志是否启用。
    bool IsFlagEnabled(FlagType ft) const;
};

#endif //__OBJECT_H__
