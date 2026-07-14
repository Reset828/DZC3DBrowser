#include "CEditLine.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <stdexcept>

// ============================================================================
// 构造和析构
// ============================================================================

CEditLine::CEditLine()
    : m_lineType(LT_LINE)
    , m_bClosed(false) {
    m_uType = OT_LINE;
}

CEditLine::CEditLine(LineType type)
    : m_lineType(type)
    , m_bClosed(type == LT_POLYGON) {
    m_uType = OT_LINE;
}

CEditLine::CEditLine(const CEditLine& other)
    : VulkanObject(other) {
    *this = other;
}

CEditLine::~CEditLine() {}

CEditLine& CEditLine::operator=(const CEditLine& other) {
    if (this != &other) {
        VulkanObject::operator=(other);
        m_lineType = other.m_lineType;
        m_vertices = other.m_vertices;
        m_bClosed = other.m_bClosed;

    }
    return *this;
}

// ============================================================================
// 类型管理
// ============================================================================

void CEditLine::SetLineType(LineType type) {
    if (m_lineType != type) {
        m_lineType = type;
        // 多边形默认闭合
        if (type == LT_POLYGON) {
            m_bClosed = true;
        }
        SetDirty(true);
    }
}

CEditLine::LineType CEditLine::GetLineType() const {
    return m_lineType;
}

// ============================================================================
// 顶点管理
// ============================================================================

void CEditLine::Clear() {
    m_vertices.clear();
    SetDirty(true);
}

bool CEditLine::IsEmpty() const {
    return m_vertices.empty();
}

size_t CEditLine::GetVertexCount() const {
    return m_vertices.size();
}

void CEditLine::AddVertex(const Vec3& vertex) {
    m_vertices.push_back(vertex);
    SetDirty(true);
}

void CEditLine::AddVertex(float x, float y, float z) {
    AddVertex(Vec3{ x, y, z });
}

void CEditLine::InsertVertex(size_t index, const Vec3& vertex) {
    if (index > m_vertices.size()) {
        throw std::out_of_range("顶点索引超出范围");
    }
    m_vertices.insert(m_vertices.begin() + index, vertex);
    SetDirty(true);
}

void CEditLine::RemoveVertex(size_t index) {
    if (index >= m_vertices.size()) {
        throw std::out_of_range("顶点索引超出范围");
    }
    m_vertices.erase(m_vertices.begin() + index);
    SetDirty(true);
}

void CEditLine::RemoveLastVertex() {
    if (!m_vertices.empty()) {
        m_vertices.pop_back();
        SetDirty(true);
    }
}

void CEditLine::SetVertex(size_t index, const Vec3& vertex) {
    if (index >= m_vertices.size()) {
        throw std::out_of_range("顶点索引超出范围");
    }
    m_vertices[index] = vertex;
    SetDirty(true);
}

Vec3 CEditLine::GetVertex(size_t index) const {
    if (index >= m_vertices.size()) {
        throw std::out_of_range("顶点索引超出范围");
    }
    return m_vertices[index];
}

Vec3& CEditLine::GetVertexRef(size_t index) {
    if (index >= m_vertices.size()) {
        throw std::out_of_range("顶点索引超出范围");
    }
    SetDirty(true);
    return m_vertices[index];
}

const Vec3& CEditLine::GetVertexRef(size_t index) const {
    if (index >= m_vertices.size()) {
        throw std::out_of_range("顶点索引超出范围");
    }
    return m_vertices[index];
}

void CEditLine::SetVertices(const std::vector<Vec3>& vertices) {
    m_vertices = vertices;
    SetDirty(true);
}

const std::vector<Vec3>& CEditLine::GetVertices() const {
    return m_vertices;
}

Vec3* CEditLine::GetVertexData() {
    return m_vertices.data();
}

const Vec3* CEditLine::GetVertexData() const {
    return m_vertices.data();
}

void CEditLine::SetClosed(bool bClosed) {
    m_bClosed = bClosed;
    SetDirty(true);
}

bool CEditLine::IsClosed() const {
    return m_bClosed;
}

// ============================================================================
// 几何计算
// ============================================================================

double CEditLine::GetLength() const {
    if (m_vertices.size() < 2) {
        return 0.0;
    }

    double length = 0.0;
    for (size_t i = 0; i < m_vertices.size() - 1; i++) {
        const Vec3& v1 = m_vertices[i];
        const Vec3& v2 = m_vertices[i + 1];
        double dx = v2.x - v1.x;
        double dy = v2.y - v1.y;
        double dz = v2.z - v1.z;
        length += std::sqrt(dx * dx + dy * dy + dz * dz);
    }

    // 如果闭合，加上首尾相连的边
    if (m_bClosed && m_vertices.size() > 2) {
        const Vec3& v1 = m_vertices.back();
        const Vec3& v2 = m_vertices.front();
        double dx = v2.x - v1.x;
        double dy = v2.y - v1.y;
        double dz = v2.z - v1.z;
        length += std::sqrt(dx * dx + dy * dy + dz * dz);
    }

    return length;
}

double CEditLine::GetArea() const {
    // 仅对多边形有效，使用鞋带公式（2D投影）
    if (m_lineType != LT_POLYGON || m_vertices.size() < 3) {
        return 0.0;
    }

    double area = 0.0;
    size_t n = m_vertices.size();

    for (size_t i = 0; i < n; i++) {
        size_t j = (i + 1) % n;
        area += m_vertices[i].x * m_vertices[j].y;
        area -= m_vertices[j].x * m_vertices[i].y;
    }

    return std::abs(area) / 2.0;
}

Vec3 CEditLine::GetCenter() const {
    if (m_vertices.empty()) {
        return Vec3{ 0, 0, 0 };
    }

    Vec3 center = { 0, 0, 0 };
    for (const auto& v : m_vertices) {
        center.x += v.x;
        center.y += v.y;
        center.z += v.z;
    }

    float inv = 1.0f / static_cast<float>(m_vertices.size());
    center.x *= inv;
    center.y *= inv;
    center.z *= inv;

    return center;
}

Vec3 CEditLine::GetNormal() const {
    // 计算多边形法向量（使用Newell方法）
    if (m_vertices.size() < 3) {
        return Vec3{ 0, 0, 1 };  // 默认向上
    }

    Vec3 normal = { 0, 0, 0 };
    size_t n = m_vertices.size();

    for (size_t i = 0; i < n; i++) {
        size_t j = (i + 1) % n;
        normal.x += (m_vertices[i].y - m_vertices[j].y) * (m_vertices[i].z + m_vertices[j].z);
        normal.y += (m_vertices[i].z - m_vertices[j].z) * (m_vertices[i].x + m_vertices[j].x);
        normal.z += (m_vertices[i].x - m_vertices[j].x) * (m_vertices[i].y + m_vertices[j].y);
    }

    // 归一化
    float length = std::sqrt(normal.x * normal.x + normal.y * normal.y + normal.z * normal.z);
    if (length > 0.00001f) {
        normal.x /= length;
        normal.y /= length;
        normal.z /= length;
    }

    return normal;
}

bool CEditLine::IsConvex() const {
    if (m_lineType != LT_POLYGON || m_vertices.size() < 3) {
        return false;
    }

    size_t n = m_vertices.size();
    bool positive = false;
    bool negative = false;

    for (size_t i = 0; i < n; i++) {
        size_t j = (i + 1) % n;
        size_t k = (j + 1) % n;

        float crossX = (m_vertices[j].x - m_vertices[i].x) * (m_vertices[k].z - m_vertices[i].z) -
                        (m_vertices[j].z - m_vertices[i].z) * (m_vertices[k].x - m_vertices[i].x);
        float crossY = (m_vertices[j].y - m_vertices[i].y) * (m_vertices[k].z - m_vertices[i].z) -
                        (m_vertices[j].z - m_vertices[i].z) * (m_vertices[k].y - m_vertices[i].y);

        if (crossX > 0 || crossY > 0) positive = true;
        if (crossX < 0 || crossY < 0) negative = true;
    }

    return !(positive && negative);
}

bool CEditLine::IsSelfIntersecting() const {
    if (m_vertices.size() < 4) {
        return false;
    }

    // 简化的自相交检测
    // 实际应用中需要更复杂的算法
    for (size_t i = 0; i < m_vertices.size() - 1; i++) {
        for (size_t j = i + 2; j < m_vertices.size() - 1; j++) {
            // 检测线段 (i, i+1) 和 (j, j+1) 是否相交
            // 这里简化为2D检测
            const Vec3& p1 = m_vertices[i];
            const Vec3& p2 = m_vertices[i + 1];
            const Vec3& p3 = m_vertices[j];
            const Vec3& p4 = m_vertices[j + 1];

            float d1 = (p2.x - p1.x) * (p3.y - p1.y) - (p2.y - p1.y) * (p3.x - p1.x);
            float d2 = (p2.x - p1.x) * (p4.y - p1.y) - (p2.y - p1.y) * (p4.x - p1.x);
            float d3 = (p4.x - p3.x) * (p1.y - p3.y) - (p4.y - p3.y) * (p1.x - p3.x);
            float d4 = (p4.x - p3.x) * (p2.y - p3.y) - (p4.y - p3.y) * (p2.x - p3.x);

            if (((d1 > 0 && d2 < 0) || (d1 < 0 && d2 > 0)) &&
                ((d3 > 0 && d4 < 0) || (d3 < 0 && d4 > 0))) {
                return true;
            }
        }
    }

    return false;
}

// ============================================================================
// 点与几何对象的关系
// ============================================================================

bool CEditLine::ContainsPoint(const Vec3& point, bool bIgnoreZ) const {
    if (m_lineType != LT_POLYGON || m_vertices.size() < 3) {
        return false;
    }

    // 射线法检测点是否在多边形内部（2D投影）
    bool inside = false;
    size_t n = m_vertices.size();

    for (size_t i = 0, j = n - 1; i < n; j = i++) {
        float yi = bIgnoreZ ? m_vertices[i].y : m_vertices[i].y;
        float xi = bIgnoreZ ? m_vertices[i].x : m_vertices[i].x;
        float yj = bIgnoreZ ? m_vertices[j].y : m_vertices[j].y;
        float xj = bIgnoreZ ? m_vertices[j].x : m_vertices[j].x;

        float yp = bIgnoreZ ? point.y : point.y;
        float xp = bIgnoreZ ? point.x : point.x;

        if (((yi > yp) != (yj > yp)) &&
            (xp < (xj - xi) * (yp - yi) / (yj - yi) + xi)) {
            inside = !inside;
        }
    }

    return inside;
}

double CEditLine::DistanceToPoint(const Vec3& point) const {
    if (m_vertices.empty()) {
        return 0.0;
    }

    if (m_vertices.size() == 1) {
        double dx = point.x - m_vertices[0].x;
        double dy = point.y - m_vertices[0].y;
        double dz = point.z - m_vertices[0].z;
        return std::sqrt(dx * dx + dy * dy + dz * dz);
    }

    double minDist = 1e30;

    for (size_t i = 0; i < m_vertices.size() - 1; i++) {
        const Vec3& p1 = m_vertices[i];
        const Vec3& p2 = m_vertices[i + 1];

        // 计算点到线段的距离
        double dx = p2.x - p1.x;
        double dy = p2.y - p1.y;
        double dz = p2.z - p1.z;
        double len2 = dx * dx + dy * dy + dz * dz;

        if (len2 < 1e-10) {
            // 线段退化为点
            double dist = std::sqrt((point.x - p1.x) * (point.x - p1.x) +
                                     (point.y - p1.y) * (point.y - p1.y) +
                                     (point.z - p1.z) * (point.z - p1.z));
            minDist = std::min(minDist, dist);
            continue;
        }

        double t = ((point.x - p1.x) * dx + (point.y - p1.y) * dy + (point.z - p1.z) * dz) / len2;
        t = std::max(0.0, std::min(1.0, t));

        double projX = p1.x + t * dx;
        double projY = p1.y + t * dy;
        double projZ = p1.z + t * dz;

        double dist = std::sqrt((point.x - projX) * (point.x - projX) +
                                 (point.y - projY) * (point.y - projY) +
                                 (point.z - projZ) * (point.z - projZ));
        minDist = std::min(minDist, dist);
    }

    return minDist;
}

// ============================================================================
// 相交检测
// ============================================================================

bool CEditLine::Intersects(const CEditLine& other) const {
    // 逐线段检测
    for (size_t i = 0; i < m_vertices.size() - 1; i++) {
        for (size_t j = 0; j < other.m_vertices.size() - 1; j++) {
            const Vec3& p1 = m_vertices[i];
            const Vec3& p2 = m_vertices[i + 1];
            const Vec3& p3 = other.m_vertices[j];
            const Vec3& p4 = other.m_vertices[j + 1];

            // 2D线段相交检测
            float d1 = (p2.x - p1.x) * (p3.y - p1.y) - (p2.y - p1.y) * (p3.x - p1.x);
            float d2 = (p2.x - p1.x) * (p4.y - p1.y) - (p2.y - p1.y) * (p4.x - p1.x);
            float d3 = (p4.x - p3.x) * (p1.y - p3.y) - (p4.y - p3.y) * (p1.x - p3.x);
            float d4 = (p4.x - p3.x) * (p2.y - p3.y) - (p4.y - p3.y) * (p2.x - p3.x);

            if (((d1 > 0 && d2 < 0) || (d1 < 0 && d2 > 0)) &&
                ((d3 > 0 && d4 < 0) || (d3 < 0 && d4 > 0))) {
                return true;
            }
        }
    }

    return false;
}

// ============================================================================
// VulkanObject接口实现
// ============================================================================

void CEditLine::Render(int iMode) {
    if (!IsVisible() || m_vertices.empty() || !m_pRender) return;

    if (IsDirty()) {
        UpdateBuffers();
        SetDirty(false);
    }

    VkCommandBuffer cmd = m_pRender->GetCurrentCommandBuffer();

    VulkanRender::DrawTopology topo;
    switch (m_lineType) {
    case LT_POLYGON: topo = VulkanRender::DT_TRIANGLE; break;
    case LT_LINE:    topo = VulkanRender::DT_LINE; break;
    case LT_POINT:   topo = VulkanRender::DT_POINT; break;
    default:         topo = VulkanRender::DT_TRIANGLE; break;
    }

    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_pRender->GetPipeline(topo));

    VkBuffer vertexBuffers[] = { m_vertexBuffer };
    VkDeviceSize offsets[] = { 0 };
    vkCmdBindVertexBuffers(cmd, 0, 1, vertexBuffers, offsets);

    vkCmdBindIndexBuffer(cmd, m_indexBuffer, 0, VK_INDEX_TYPE_UINT32);

    m_pRender->DrawIndexed(m_indexCount);
}

// ============================================================================
// 缓冲区更新
// ============================================================================


void CEditLine::UpdateBuffers() {
    if (!m_pRender || m_vertices.empty()) return;

    std::vector<VulkanVertex> vertices;
    std::vector<uint32_t> indices;

    BuildVertices(vertices);
    BuildIndices(indices);

    CreateVertexBuffer(vertices);
    CreateIndexBuffer(indices);
}

void CEditLine::BuildVertices(std::vector<VulkanVertex>& vertices) const {
    Vec4 color = GetColor();
    vertices.reserve(m_vertices.size());

    for (const auto& v : m_vertices) {
        VulkanVertex vertex;
        vertex.position = v;
        vertex.color = Vec3{ color.x, color.y, color.z };
        vertex.texCoord = Vec2{ 0, 0 };
        vertices.push_back(vertex);
    }
}

void CEditLine::BuildIndices(std::vector<uint32_t>& indices) const {
    indices.clear();

    if (m_vertices.empty()) return;

    switch (m_lineType) {
    case LT_POINT:
        // 点：每个顶点一个索引
        for (uint32_t i = 0; i < m_vertices.size(); i++) {
            indices.push_back(i);
        }
        break;

    case LT_LINE:
        // 线：连续线段
        for (uint32_t i = 0; i < m_vertices.size() - 1; i++) {
            indices.push_back(i);
            indices.push_back(i + 1);
        }
        break;

    case LT_POLYGON:
        // 多边形：三角扇形
        for (uint32_t i = 1; i < m_vertices.size() - 1; i++) {
            indices.push_back(0);
            indices.push_back(i);
            indices.push_back(i + 1);
        }
        // 如果闭合，添加首尾三角形
        if (m_bClosed && m_vertices.size() > 2) {
            indices.push_back(0);
            indices.push_back(static_cast<uint32_t>(m_vertices.size() - 1));
            indices.push_back(1);
        }
        break;
    }
}
