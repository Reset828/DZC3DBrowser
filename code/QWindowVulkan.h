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

    VkSurfaceKHR GetSurface() const;
    VkInstance GetVkInstance() const;
    void SetRenderer(VKRender* renderer);

signals:
    void vulkanReady();

protected:
    void exposeEvent(QExposeEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;

private:
    bool CreateVulkanSurface();

    VKRender* m_renderer;
    VkInstance m_instance;
    VkSurfaceKHR m_surface;
    bool m_initialized;
};

#endif // __QWINDOW_VULKAN_H__
