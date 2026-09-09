#include "Layer.h"
#include <algorithm>
#include <shared_mutex>

Layer::Layer() {
    m_uType = OT_LAYER;
}

Layer::~Layer() {
    Clear();
}

bool Layer::IsEmpty() const {
    std::shared_lock lock(m_mutex);
    return m_arrChild.empty();
}

uint32_t Layer::GetCount() const {
    std::shared_lock lock(m_mutex);
    return static_cast<uint32_t>(m_arrChild.size());
}

SceneObject* Layer::GetChild(uint32_t index) {
    std::shared_lock lock(m_mutex);
    if (index < m_arrChild.size()) {
        return m_arrChild[index];
    }
    return nullptr;
}

const SceneObject* Layer::GetChild(uint32_t index) const {
    std::shared_lock lock(m_mutex);
    if (index < m_arrChild.size()) {
        return m_arrChild[index];
    }
    return nullptr;
}

int Layer::FindChild(const SceneObject* pObject) const {
    if (!pObject) return -1;

    std::shared_lock lock(m_mutex);
    for (size_t i = 0; i < m_arrChild.size(); i++) {
        if (m_arrChild[i] == pObject) {
            return static_cast<int>(i);
        }
    }
    return -1;
}

void Layer::Clear() {
    std::unique_lock lock(m_mutex);
    for (auto* child : m_arrChild) {
        if (child) {
            delete child;
        }
    }
    m_arrChild.clear();
}

void Layer::AddChild(SceneObject* pObject) {
    if (!pObject) return;

    std::lock_guard<std::shared_mutex> lock(m_mutex);

    pObject->SetParent(this);
    m_arrChild.push_back(pObject);
}

void Layer::RemoveChild(uint32_t index) {
    std::unique_lock lock(m_mutex);
    if (index >= m_arrChild.size()) return;

    SceneObject* child = m_arrChild[index];
    if (child) {
        delete child;
    }
    m_arrChild.erase(m_arrChild.begin() + index);
}

void Layer::RemoveChild(SceneObject* pObject) {
    if (!pObject) return;

    std::unique_lock lock(m_mutex);
    auto it = std::find(m_arrChild.begin(), m_arrChild.end(), pObject);
    if (it != m_arrChild.end()) {
        delete *it;
        m_arrChild.erase(it);
    }
}

void Layer::Render(int iMode) {
    if (!IsVisible()) return;

    std::shared_lock lock(m_mutex);
    for (auto* child : m_arrChild) {
        if (child && child->IsVisible()) {
            child->Render(iMode);
        }
    }
}
