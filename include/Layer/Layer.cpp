#include "Layer.h"
#include <algorithm>
#include <shared_mutex>

Layer::Layer() {
    m_uType = OT_LAYER;
}

Layer::~Layer() {
    Clear();
}

// 判断子对象列表是否为空。
bool Layer::IsEmpty() const {
    std::shared_lock lock(m_mutex);
    return m_arrChild.empty();
}

// 获取子对象数量。
uint32_t Layer::GetCount() const {
    std::shared_lock lock(m_mutex);
    return static_cast<uint32_t>(m_arrChild.size());
}

// 按索引返回子对象。
Object* Layer::GetChild(uint32_t index) {
    std::shared_lock lock(m_mutex);
    if (index < m_arrChild.size()) {
        return m_arrChild[index];
    }
    return nullptr;
}

// 获取指定索引的只读子对象。
const Object* Layer::GetChild(uint32_t index) const {
    std::shared_lock lock(m_mutex);
    if (index < m_arrChild.size()) {
        return m_arrChild[index];
    }
    return nullptr;
}

// 查找子对象的索引。
int Layer::FindChild(const Object* pObject) const {
    if (!pObject) return -1;

    std::shared_lock lock(m_mutex);
    for (size_t i = 0; i < m_arrChild.size(); i++) {
        if (m_arrChild[i] == pObject) {
            return static_cast<int>(i);
        }
    }
    return -1;
}

// 清理当前对象内容。
void Layer::Clear() {
    std::unique_lock lock(m_mutex);
    for (auto* child : m_arrChild) {
        if (child) {
            delete child;
        }
    }
    m_arrChild.clear();
}

// 把对象加入子列表并设置父指针。
void Layer::AddChild(Object* pObject) {
    if (!pObject) return;

    std::lock_guard<std::shared_mutex> lock(m_mutex);

    pObject->SetParent(this);
    m_arrChild.push_back(pObject);
}

// 按索引或指针删除子对象。
void Layer::RemoveChild(uint32_t index) {
    std::unique_lock lock(m_mutex);
    if (index >= m_arrChild.size()) return;

    Object* child = m_arrChild[index];
    if (child) {
        delete child;
    }
    m_arrChild.erase(m_arrChild.begin() + index);
}

// 按索引或指针删除子对象。
void Layer::RemoveChild(Object* pObject) {
    if (!pObject) return;

    std::unique_lock lock(m_mutex);
    auto it = std::find(m_arrChild.begin(), m_arrChild.end(), pObject);
    if (it != m_arrChild.end()) {
        delete *it;
        m_arrChild.erase(it);
    }
}

// 绘制自身；Layer 则遍历子对象。
void Layer::Render(int iMode) {
    if (!IsVisible()) return;

    std::shared_lock lock(m_mutex);
    for (auto* child : m_arrChild) {
        if (child && child->IsVisible()) {
            child->Render(iMode);
        }
    }
}
