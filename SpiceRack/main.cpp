#include "MainWindow.h"

#include <QtCore/QTimer>
#include <QtWidgets/QApplication>

#include <algorithm>
#include <string_view>

int main(int argc, char** argv) {
    const bool smokeTest = std::any_of(argv + 1, argv + argc, [](const char* argument) {
        return std::string_view(argument) == "--smoke-test";
    });

    QApplication application(argc, argv);
    QApplication::setOrganizationName("jahorta");
    QApplication::setApplicationName("SpiceRack");
    QApplication::setApplicationDisplayName("SpiceRack");

    SpiceRackMainWindow window;
    window.show();

    if (smokeTest) {
        QTimer::singleShot(0, &application, &QCoreApplication::quit);
    }
    return application.exec();
}
