#include <QApplication>
#include "MainWindow.h"

// 启动 Qt 应用程序。
int main(int argc, char *argv[]) {
    QApplication app(argc, argv);
    QCoreApplication::setOrganizationName(QStringLiteral("VulkanReference"));
    QCoreApplication::setApplicationName(QStringLiteral("VulkanReference"));
    MainWindow window;
    window.show();
    return app.exec();
}
