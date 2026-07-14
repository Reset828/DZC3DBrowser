#ifndef __CEditLine_H__
#define __CEditLine_H__

#include "VulkanObject.h"
#include <vector>

// 点、线、多边形几何对象
// 支持三种几何元素类型，通过类型字段区分
class CEditLine : public VulkanObject {
public:
    // 几何元素类型
    enum LineType {
        LT_POINT = 0,   // 点
        LT_LINE,        // 线（折线）
        LT_POLYGON      // 多边形（自动闭合）
    };

    CEditLine();
    CEditLine(LineType type);
    CEditLine(const CEditLine& other);
    virtual ~CEditLine();

    CEditLine& operator=(const CEditLine& other);



    // 基本操作
    void SetLineType(LineType type);
    LineType GetLineType() const;

    // 顶点管理
    void Clear();
    bool IsEmpty() const;
    size_t GetVertexCount() const;

    // 添加顶点
    void AddVertex(const Vec3& vertex);
    void AddVertex(float x, float y, float z);
    void InsertVertex(size_t index, const Vec3& vertex);

    // 删除顶点
    void RemoveVertex(size_t index);
    void RemoveLastVertex();

    // 设置顶点
    void SetVertex(size_t index, const Vec3& vertex);
    Vec3 GetVertex(size_t index) const;
    Vec3& GetVertexRef(size_t index);
    const Vec3& GetVertexRef(size_t index) const;

    // 批量设置
    void SetVertices(const std::vector<Vec3>& vertices);
    const std::vector<Vec3>& GetVertices() const;

    Vec3* GetVertexData();
    const Vec3* GetVertexData() const;

    // 闭合控制（仅对多边形有效）
    void SetClosed(bool bClosed);
    bool IsClosed() const;

    // 几何计算
    double GetLength() const;          // 获取总长度（线）或周长（多边形）
    double GetArea() const;            // 获取面积（仅多边形）
    Vec3 GetCenter() const;            // 获取中心点
    Vec3 GetNormal() const;            // 获取法向量（多边形）
    bool IsConvex() const;             // 是否为凸多边形
    bool IsSelfIntersecting() const;   // 是否自相交

    // 点与几何对象的关系
    bool ContainsPoint(const Vec3& point, bool bIgnoreZ = true) const;  // 点是否在内部
    double DistanceToPoint(const Vec3& point) const;                     // 到点的距离

    // 相交检测
    bool Intersects(const CEditLine& other) const;

    void Render(int iMode = 0) override;


    void UpdateBuffers();

protected:
    void BuildVertices(std::vector<VulkanVertex>& vertices) const;
    void BuildIndices(std::vector<uint32_t>& indices) const;

protected:
    LineType m_lineType;
    std::vector<Vec3> m_vertices;
    bool m_bClosed;
};

// 类型别名
using CEditPoint = CEditLine;    // 点类型
using CEditPolygon = CEditLine;  // 多边形类型

#endif //__CEditLine_H__
