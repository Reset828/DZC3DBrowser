#ifndef __LAYER_H__
#define __LAYER_H__

#include "Object/Object.h"
#include <vector>
#include <shared_mutex>

// 子对象容器：维护父子关系并遍历绘制，不持有 GPU 资源。
class Layer : public Object {
public:
    Layer();
    virtual ~Layer();

    // 查询子对象列表是否为空。
    bool IsEmpty() const;
    // 返回子对象数量。
    uint32_t GetCount() const;

    // 按索引返回子对象。
    Object* GetChild(uint32_t index);
    // 按索引返回子对象。
    const Object* GetChild(uint32_t index) const;
    // 返回子对象索引，找不到则 -1。
    int FindChild(const Object* pObject) const;

    // 删除并清空全部子对象。
    virtual void Clear();
    // 把对象加入子列表并设置父指针。
    virtual void AddChild(Object* pObject);

    // 按索引或指针删除子对象。
    virtual void RemoveChild(uint32_t index);
    // 按索引或指针删除子对象。
    virtual void RemoveChild(Object* pObject);

    // 标记自身及所有后代的世界矩阵需要重算。
    void MarkWorldTransformDirty() override;
    // 自顶向下刷新自身与所有子对象的世界矩阵；返回自身是否变化。
    bool UpdateWorldTransforms(const Mat4* parentWorld, bool parentChanged) override;

    // 子树世界包围盒（任务 2.2）：自身几何 ∪ 所有子节点的子树包围盒。
    // worldMatrix 为“本 Layer 局部 -> 目标空间”的矩阵；子节点的矩阵在内部递推。
    Aabb GetWorldBounds(const Mat4& worldMatrix) const override;

    // 遍历可见子对象并绘制。
    void Render(int iMode = 0) override;

    std::vector<Object*> m_arrChild;

protected:
    mutable std::shared_mutex m_mutex;
};

#endif //__LAYER_H__
