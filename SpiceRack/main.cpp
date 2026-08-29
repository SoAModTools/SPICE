#include "MainWindow.h"

#include <QtCore/QTimer>
#include <QtWidgets/QApplication>

#include <filesystem>
#include <optional>
#include <string_view>

int main(int argc, char** argv) {
    bool smokeTest = false;
    std::optional<std::filesystem::path> smokePath{};
    for (int index = 1; index < argc; ++index) {
        if (std::string_view(argv[index]) == "--smoke-test") {
            smokeTest = true;
            if (index + 1 < argc && std::string_view(argv[index + 1]).front() != '-') {
                smokePath = std::filesystem::path(argv[++index]);
            }
        }
    }

    QApplication application(argc, argv);
    QApplication::setOrganizationName("jahorta");
    QApplication::setApplicationName("SpiceRack");
    QApplication::setApplicationDisplayName("SpiceRack");

    SpiceRackMainWindow window;
    window.show();

    if (smokeTest) {
        if (smokePath.has_value()) {
            QTimer::singleShot(0, &window, [&window, &application, path = *smokePath]() {
                window.openDocument(path,
                    [&application](const bool success) { application.exit(success ? 0 : 1); }, false);
            });
        } else {
            QTimer::singleShot(0, &application, &QCoreApplication::quit);
        }
    }
    return application.exec();
}
