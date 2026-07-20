#include "VulkanLayer.h"
#include <algorithm>
#include <shared_mutex>


VulkanLayer::VulkanLayer() {
    m_uType = OT_LAYER;
}

VulkanLayer::~VulkanLayer() {
    Clear();
}


bool VulkanLayer::IsEmpty() const {
    std::shared_lock lock(m_mutex);
    return m_arrChild.empty();
}

uint32_t VulkanLayer::GetCount() const {
    std::shared_lock lock(m_mutex);
    return static_cast<uint32_t>(m_arrChild.size());
}

VulkanObject* VulkanLayer::GetChild(uint32_t index) {
    std::shared_lock lock(m_mutex);
    if (index < m_arrChild.size()) {
        return m_arrChild[index];
    }
    return nullptr;
}

const VulkanObject* VulkanLayer::GetChild(uint32_t index) const {
    std::shared_lock lock(m_mutex);
    if (index < m_arrChild.size()) {
        return m_arrChild[index];
    }
    return nullptr;
}

int VulkanLayer::FindChild(const VulkanObject* pObject) const {
    if (!pObject) return -1;

    std::shared_lock lock(m_mutex);
    for (size_t i = 0; i < m_arrChild.size(); i++) {
        if (m_arrChild[i] == pObject) {
            return static_cast<int>(i);
        }
    }
    return -1;
}


void VulkanLayer::Clear() {
    std::unique_lock lock(m_mutex);
    for (auto* child : m_arrChild) {
        if (child) {
            delete child;
        }
    }
    m_arrChild.clear();
}

void VulkanLayer::AddChild(VulkanObject* pObject) {
    if (!pObject) return;

    std::lock_guard<std::shared_mutex> lock(m_mutex);

    pObject->SetParent(this);
    m_arrChild.push_back(pObject);
}

void VulkanLayer::RemoveChild(uint32_t index) {
    std::unique_lock lock(m_mutex);
    if (index >= m_arrChild.size()) return;

    VulkanObject* child = m_arrChild[index];
    if (child) {
        delete child;
    }
    m_arrChild.erase(m_arrChild.begin() + index);
}

void VulkanLayer::RemoveChild(VulkanObject* pObject) {
    if (!pObject) return;

    std::unique_lock lock(m_mutex);
    auto it = std::find(m_arrChild.begin(), m_arrChild.end(), pObject);
    if (it != m_arrChild.end()) {
        delete *it;
        m_arrChild.erase(it);
    }
}



void VulkanLayer::Render(int iMode) {
    if (!IsVisible()) return;

    std::shared_lock lock(m_mutex);
    for (auto* child : m_arrChild) {
        if (child && child->IsVisible()) {
            child->Render(iMode);
        }
    }
}


