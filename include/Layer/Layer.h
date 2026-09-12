#ifndef __LAYER_H__
#define __LAYER_H__

#include "Object/Object.h"
#include <vector>
#include <shared_mutex>

class Layer : public Object {
public:
    Layer();
    virtual ~Layer();

    bool IsEmpty() const;
    uint32_t GetCount() const;

    Object* GetChild(uint32_t index);
    const Object* GetChild(uint32_t index) const;
    int FindChild(const Object* pObject) const;

    virtual void Clear();
    virtual void AddChild(Object* pObject);

    virtual void RemoveChild(uint32_t index);
    virtual void RemoveChild(Object* pObject);

    void Render(int iMode = 0) override;

    std::vector<Object*> m_arrChild;

protected:
    mutable std::shared_mutex m_mutex;
};

#endif //__LAYER_H__
