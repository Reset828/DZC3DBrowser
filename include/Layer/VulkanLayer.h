#ifndef __VULKAN_LAYER_H__
#define __VULKAN_LAYER_H__

#include "Object/VulkanObject.h"
#include <vector>
#include <shared_mutex>

class VulkanLayer : public VulkanObject {
public:
    VulkanLayer();
    virtual ~VulkanLayer();

    bool IsEmpty() const;
    uint32_t GetCount() const;

    VulkanObject* GetChild(uint32_t index);
    const VulkanObject* GetChild(uint32_t index) const;
    int FindChild(const VulkanObject* pObject) const;

    virtual void Clear();
    virtual void AddChild(VulkanObject* pObject);

    virtual void RemoveChild(uint32_t index);
    virtual void RemoveChild(VulkanObject* pObject);

    void Render(int iMode = 0) override;

    std::vector<VulkanObject*> m_arrChild;

protected:
    mutable std::shared_mutex m_mutex;
};

#endif //__VULKAN_LAYER_H__
