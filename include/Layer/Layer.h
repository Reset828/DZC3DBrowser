#ifndef __LAYER_H__
#define __LAYER_H__

#include "Object/SceneObject.h"
#include <vector>
#include <shared_mutex>

class Layer : public SceneObject {
public:
    Layer();
    virtual ~Layer();

    bool IsEmpty() const;
    uint32_t GetCount() const;

    SceneObject* GetChild(uint32_t index);
    const SceneObject* GetChild(uint32_t index) const;
    int FindChild(const SceneObject* pObject) const;

    virtual void Clear();
    virtual void AddChild(SceneObject* pObject);

    virtual void RemoveChild(uint32_t index);
    virtual void RemoveChild(SceneObject* pObject);

    void Render(int iMode = 0) override;

    std::vector<SceneObject*> m_arrChild;

protected:
    mutable std::shared_mutex m_mutex;
};

#endif //__LAYER_H__
