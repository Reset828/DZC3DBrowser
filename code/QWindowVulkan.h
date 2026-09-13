#ifndef __QWINDOW_VULKAN_H__
#define __QWINDOW_VULKAN_H__
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#define VK_USE_PLATFORM_WIN32_KHR

#include <QWindow>
#include <vulkan/vulkan.h>
#include <vulkan/vulkan_win32.h> 

class VKRender;

class QWindowVulkan : public QWindow {
    Q_OBJECT
public:
    explicit QWindowVulkan(VKRender* renderer);
    ~QWindowVulkan();

    // 返回 Win32 VkSurfaceKHR。
    VkSurfaceKHR GetSurface() const;
    // 返回本窗口创建的 VkInstance。
    VkInstance GetVkInstance() const;
    // 更换窗口绑定的渲染器。
    void SetRenderer(VKRender* renderer);

signals:
    // 实例与表面就绪后发出。
    void vulkanReady();

protected:
    // 首次露出时创建后端并 Initialize。
    void exposeEvent(QExposeEvent* event) override;
    // 窗口尺寸变化时更新帧缓冲大小。
    void resizeEvent(QResizeEvent* event) override;

private:
    // 创建 VkInstance 与 Win32 表面。
    bool CreateVulkanSurface();

    VKRender* m_renderer;
    VkInstance m_instance;
    VkSurfaceKHR m_surface;
    bool m_initialized;
};

#endif // __QWINDOW_VULKAN_H__
