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

    // 遍历可见子对象并绘制。
    void Render(int iMode = 0) override;

    std::vector<Object*> m_arrChild;

protected:
    mutable std::shared_mutex m_mutex;
};

#endif //__LAYER_H__
