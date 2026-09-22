#ifndef __GIZMO_H__
#define __GIZMO_H__

#include <cstdint>
#include <vector>

#include "Math/EngineTypes.h"
#include "Math/Ray.h"

// Transform Gizmo（任务 2.4）：平移 / 旋转 / 缩放操纵柄的放置、几何与拾取/拖拽数学。
//
// 本模块与后端解耦（不含 Vulkan / OpenGL / Qt 头），只负责：
//   1. 依据“放置帧（原点 + 三个单位轴 + 轴长）”生成线段几何（归一化场景空间）。
//   2. 用归一化场景空间射线做轴拾取（平移/缩放的轴线、旋转的圆环）。
//   3. 依据射线求解沿轴参数 / 绕轴角度，供窗口层换算成节点局部变换增量。
//
// 坐标约定（与项目一致）：归一化场景空间 Z-up 右手系；射线在世界（归一化场景空间）中。

// Gizmo 模式。
enum class GizmoMode {
    Translate = 0,
    Rotate = 1,
    Scale = 2
};

// Gizmo 轴。None 表示未命中任何轴。
enum class GizmoAxis {
    None = -1,
    X = 0,
    Y = 1,
    Z = 2
};

// Gizmo 顶点：归一化场景空间位置 + RGBA 颜色（叠加层，无光照）。
struct GizmoVertex {
    float position[3];
    float color[4];
};

// Gizmo 放置帧：原点 + 三个单位轴（归一化场景空间）+ 轴长（世界单位，对应固定屏幕像素）。
struct GizmoFrame {
    Vec3 origin = { 0.0f, 0.0f, 0.0f };
    Vec3 axisX = { 1.0f, 0.0f, 0.0f };
    Vec3 axisY = { 0.0f, 1.0f, 0.0f };
    Vec3 axisZ = { 0.0f, 0.0f, 1.0f };
    float axisLength = 1.0f;       // 轴长（世界单位，已按屏幕像素换算）
    float worldPerPixel = 0.001f;  // 每屏幕像素对应的世界长度（拾取阈值换算用）
};

// 返回某轴的方向（归一化场景空间单位向量）。
Vec3 GizmoAxisDirection(const GizmoFrame& frame, GizmoAxis axis);
// 返回某轴颜色（X 红 / Y 绿 / Z 蓝；highlighted 时用高亮黄）。
Vec4 GizmoAxisColor(GizmoAxis axis, bool highlighted);

// 生成 Gizmo 线段几何（每两个顶点构成一段线）。mode 决定外观：
//   平移：三根轴 + 箭头；旋转：三个圆环；缩放：三根轴 + 末端小方块。
// highlighted 轴用高亮色。
void BuildGizmoGeometry(const GizmoFrame& frame, GizmoMode mode,
                        GizmoAxis highlighted, std::vector<GizmoVertex>& out);

// 轴拾取：给定归一化场景空间射线，返回命中的轴（None = 未命中）。
// pickPixelRadius 为屏幕像素拾取半径，按 frame.worldPerPixel 换算成世界阈值。
GizmoAxis PickGizmoAxis(const GizmoFrame& frame, GizmoMode mode, const Ray& ray,
                        float pickPixelRadius);

// 平移/缩放：返回射线在轴（过原点、方向 = 该轴单位向量）上的最近参数（世界距离）。
// 用于把鼠标移动换算成沿轴位移 / 缩放比例。
float GizmoAxisParam(const GizmoFrame& frame, GizmoAxis axis, const Ray& ray);

// 旋转：返回射线与旋转平面（过原点、法线 = 轴）交点在轴周围的方位角（度）。
// 命中失败（射线与平面平行或反向）返回 false。
bool GizmoRotateAngle(const GizmoFrame& frame, GizmoAxis axis, const Ray& ray,
                      float& angleDegrees);

#endif //__GIZMO_H__
