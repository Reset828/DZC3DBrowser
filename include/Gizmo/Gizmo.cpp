#include "Gizmo/Gizmo.h"

#include <cmath>

namespace {

constexpr float kPi = 3.14159265358979323846f;

// 向量工具（本模块不依赖 glm，保持后端无关）。
Vec3 Add(const Vec3& a, const Vec3& b) { return Vec3{ a.x + b.x, a.y + b.y, a.z + b.z }; }
Vec3 Sub(const Vec3& a, const Vec3& b) { return Vec3{ a.x - b.x, a.y - b.y, a.z - b.z }; }
Vec3 Mul(const Vec3& a, float s) { return Vec3{ a.x * s, a.y * s, a.z * s }; }
float Dot(const Vec3& a, const Vec3& b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
Vec3 Cross(const Vec3& a, const Vec3& b) {
    return Vec3{ a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x };
}
float Length(const Vec3& v) { return std::sqrt(Dot(v, v)); }
Vec3 Normalize(const Vec3& v) {
    const float len = Length(v);
    if (len <= 1.0e-8f) return Vec3{ 0.0f, 0.0f, 0.0f };
    return Vec3{ v.x / len, v.y / len, v.z / len };
}

// 由单位法线 n 构造正交基 (a, b)，满足 a、b 与 n 两两垂直。
void MakeBasis(const Vec3& n, Vec3& a, Vec3& b) {
    const Vec3 helper = (std::fabs(n.z) < 0.9f) ? Vec3{ 0.0f, 0.0f, 1.0f }
                                                : Vec3{ 1.0f, 0.0f, 0.0f };
    a = Normalize(Cross(helper, n));
    b = Cross(n, a);
}

void PushVertex(std::vector<GizmoVertex>& out, const Vec3& p, const Vec4& c) {
    GizmoVertex v{};
    v.position[0] = p.x;
    v.position[1] = p.y;
    v.position[2] = p.z;
    v.color[0] = c.x;
    v.color[1] = c.y;
    v.color[2] = c.z;
    v.color[3] = c.w;
    out.push_back(v);
}

void AddLine(std::vector<GizmoVertex>& out, const Vec3& a, const Vec3& b, const Vec4& c) {
    PushVertex(out, a, c);
    PushVertex(out, b, c);
}

}  // namespace

// 返回某轴的方向（归一化场景空间单位向量）。
Vec3 GizmoAxisDirection(const GizmoFrame& frame, GizmoAxis axis) {
    switch (axis) {
    case GizmoAxis::X: return Normalize(frame.axisX);
    case GizmoAxis::Y: return Normalize(frame.axisY);
    case GizmoAxis::Z: return Normalize(frame.axisZ);
    default: return Vec3{ 0.0f, 0.0f, 0.0f };
    }
}

// 返回某轴颜色（X 红 / Y 绿 / Z 蓝；highlighted 时用高亮黄）。
Vec4 GizmoAxisColor(GizmoAxis axis, bool highlighted) {
    if (highlighted) return Vec4{ 1.0f, 0.9f, 0.1f, 1.0f };
    switch (axis) {
    case GizmoAxis::X: return Vec4{ 0.90f, 0.16f, 0.16f, 1.0f };
    case GizmoAxis::Y: return Vec4{ 0.16f, 0.80f, 0.20f, 1.0f };
    case GizmoAxis::Z: return Vec4{ 0.20f, 0.42f, 0.95f, 1.0f };
    default: return Vec4{ 0.8f, 0.8f, 0.8f, 1.0f };
    }
}

// 生成 Gizmo 线段几何（每两个顶点构成一段线）。
void BuildGizmoGeometry(const GizmoFrame& frame, GizmoMode mode,
                        GizmoAxis highlighted, std::vector<GizmoVertex>& out) {
    out.clear();
    const GizmoAxis axes[3] = { GizmoAxis::X, GizmoAxis::Y, GizmoAxis::Z };
    const float length = frame.axisLength > 1.0e-6f ? frame.axisLength : 1.0f;

    for (GizmoAxis axis : axes) {
        const Vec3 dir = GizmoAxisDirection(frame, axis);
        if (Length(dir) <= 1.0e-6f) continue;
        const Vec4 color = GizmoAxisColor(axis, axis == highlighted);
        const Vec3 tip = Add(frame.origin, Mul(dir, length));

        if (mode == GizmoMode::Rotate) {
            // 圆环：在垂直于轴的平面内按圆周分段画线。
            Vec3 a{}, b{};
            MakeBasis(dir, a, b);
            const int segments = 64;
            const float radius = length;
            Vec3 prev = Add(frame.origin, Mul(a, radius));
            for (int i = 1; i <= segments; ++i) {
                const float angle = (2.0f * kPi) * static_cast<float>(i) /
                                    static_cast<float>(segments);
                const Vec3 p = Add(frame.origin,
                    Add(Mul(a, std::cos(angle) * radius), Mul(b, std::sin(angle) * radius)));
                AddLine(out, prev, p, color);
                prev = p;
            }
            continue;
        }

        // 平移 / 缩放：轴杆。
        AddLine(out, frame.origin, tip, color);

        Vec3 a{}, b{};
        MakeBasis(dir, a, b);
        if (mode == GizmoMode::Translate) {
            // 箭头：从轴尖回退，在四个方向画短斜线。
            const Vec3 base = Add(frame.origin, Mul(dir, length * 0.82f));
            const float spread = length * 0.06f;
            AddLine(out, tip, Add(base, Mul(a, spread)), color);
            AddLine(out, tip, Add(base, Mul(a, -spread)), color);
            AddLine(out, tip, Add(base, Mul(b, spread)), color);
            AddLine(out, tip, Add(base, Mul(b, -spread)), color);
        } else {
            // 缩放：轴尖一个线框小方块。
            const float half = length * 0.055f;
            const Vec3 n = dir;
            Vec3 corner[8];
            int idx = 0;
            for (int sz = -1; sz <= 1; sz += 2) {
                for (int sy = -1; sy <= 1; sy += 2) {
                    for (int sx = -1; sx <= 1; sx += 2) {
                        corner[idx++] = Add(tip,
                            Add(Mul(n, half * sz), Add(Mul(a, half * sx), Mul(b, half * sy))));
                    }
                }
            }
            // 12 条棱：按 (sx,sy,sz) 位序索引。
            const int edges[12][2] = {
                {0,1},{2,3},{4,5},{6,7},   // 沿 a 方向
                {0,2},{1,3},{4,6},{5,7},   // 沿 b 方向
                {0,4},{1,5},{2,6},{3,7}    // 沿 n 方向
            };
            for (const auto& e : edges) {
                AddLine(out, corner[e[0]], corner[e[1]], color);
            }
        }
    }
}

// 轴拾取。
GizmoAxis PickGizmoAxis(const GizmoFrame& frame, GizmoMode mode, const Ray& ray,
                        float pickPixelRadius) {
    const float length = frame.axisLength > 1.0e-6f ? frame.axisLength : 1.0f;
    const float worldPerPixel = frame.worldPerPixel > 1.0e-8f ? frame.worldPerPixel : 0.001f;
    const float threshold = pickPixelRadius * worldPerPixel;

    const GizmoAxis axes[3] = { GizmoAxis::X, GizmoAxis::Y, GizmoAxis::Z };
    GizmoAxis best = GizmoAxis::None;
    float bestError = threshold;

    for (GizmoAxis axis : axes) {
        const Vec3 dir = GizmoAxisDirection(frame, axis);
        if (Length(dir) <= 1.0e-6f) continue;

        if (mode == GizmoMode::Rotate) {
            Vec3 hit{};
            if (!RayIntersectsPlane(ray, frame.origin, dir, &hit)) continue;
            const float radius = Length(Sub(hit, frame.origin));
            const float error = std::fabs(radius - length);
            if (error < bestError) {
                bestError = error;
                best = axis;
            }
        } else {
            float distance = 0.0f;
            const float param = RayLineClosestParam(ray, frame.origin, dir, &distance);
            // 仅当最近点落在轴杆范围内才算命中。
            if (param < -0.05f * length || param > 1.05f * length) continue;
            if (distance < bestError) {
                bestError = distance;
                best = axis;
            }
        }
    }
    return best;
}

// 平移/缩放：射线在轴上的最近参数。
float GizmoAxisParam(const GizmoFrame& frame, GizmoAxis axis, const Ray& ray) {
    const Vec3 dir = GizmoAxisDirection(frame, axis);
    if (Length(dir) <= 1.0e-6f) return 0.0f;
    return RayLineClosestParam(ray, frame.origin, dir, nullptr);
}

// 旋转：射线与旋转平面交点相对轴的方位角（度）。
bool GizmoRotateAngle(const GizmoFrame& frame, GizmoAxis axis, const Ray& ray,
                      float& angleDegrees) {
    const Vec3 dir = GizmoAxisDirection(frame, axis);
    if (Length(dir) <= 1.0e-6f) return false;

    Vec3 hit{};
    if (!RayIntersectsPlane(ray, frame.origin, dir, &hit)) return false;

    Vec3 a{}, b{};
    MakeBasis(dir, a, b);
    const Vec3 v = Sub(hit, frame.origin);
    angleDegrees = std::atan2(Dot(v, b), Dot(v, a)) * 180.0f / kPi;
    return true;
}
