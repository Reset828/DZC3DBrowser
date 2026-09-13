#include <QApplication>
#include "MainWindow.h"

// 启动 Qt 应用程序。
int main(int argc, char *argv[]) {
    QApplication app(argc, argv);
    QCoreApplication::setOrganizationName(QStringLiteral("DZC3DBrowser"));
    QCoreApplication::setApplicationName(QStringLiteral("DZC3DBrowser"));
    MainWindow window;
    window.show();
    return app.exec();
}
