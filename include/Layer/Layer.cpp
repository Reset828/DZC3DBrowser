#include "Layer.h"
#include <algorithm>
#include <shared_mutex>

Layer::Layer() {
    m_uType = OT_LAYER;
}

Layer::~Layer() {
    Clear();
}

// 查询子对象列表是否为空。
bool Layer::IsEmpty() const {
    std::shared_lock lock(m_mutex);
    return m_arrChild.empty();
}

// 返回子对象数量。
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

// 按索引返回子对象。
const Object* Layer::GetChild(uint32_t index) const {
    std::shared_lock lock(m_mutex);
    if (index < m_arrChild.size()) {
        return m_arrChild[index];
    }
    return nullptr;
}

// 返回子对象索引，找不到则 -1。
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

// 删除并清空全部子对象。
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
    // 新加入的子对象世界矩阵需要重算。
    pObject->MarkWorldTransformDirty();
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

// 标记自身及所有后代的世界矩阵需要重算。
void Layer::MarkWorldTransformDirty() {
    SetDirty(true);
    std::shared_lock lock(m_mutex);
    for (auto* child : m_arrChild) {
        if (child) {
            child->MarkWorldTransformDirty();
        }
    }
}

// 自顶向下刷新自身与所有子对象的世界矩阵。
bool Layer::UpdateWorldTransforms(const Mat4* parentWorld, bool parentChanged) {
    const bool changed = Object::UpdateWorldTransforms(parentWorld, parentChanged);
    // 始终访问子节点：即使本层未变，子节点也可能自身为脏。
    std::shared_lock lock(m_mutex);
    for (auto* child : m_arrChild) {
        if (child) {
            child->UpdateWorldTransforms(&m_worldMatrix, changed);
        }
    }
    return changed;
}

// 子树世界包围盒（任务 2.2）：自身几何 ∪ 所有子节点的子树包围盒。
Aabb Layer::GetWorldBounds(const Mat4& worldMatrix) const {
    Aabb result = Object::GetWorldBounds(worldMatrix);
    std::shared_lock lock(m_mutex);
    for (auto* child : m_arrChild) {
        if (!child) continue;
        const Mat4 childWorld = TransformMultiply(worldMatrix, child->GetLocalTransform().ToMatrix());
        result = AabbUnion(result, child->GetWorldBounds(childWorld));
    }
    return result;
}

// 遍历可见子对象并绘制。
void Layer::Render(int iMode) {
    if (!IsVisible()) return;

    std::shared_lock lock(m_mutex);
    for (auto* child : m_arrChild) {
        if (child && child->IsVisible()) {
            child->Render(iMode);
        }
    }
}
