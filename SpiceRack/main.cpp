#include "MainWindow.h"

#include <QtCore/QTimer>
#include <QtWidgets/QApplication>

#include <filesystem>
#include <string_view>
#include <vector>

int main(int argc, char** argv) {
    bool smokeTest = false;
    std::vector<std::filesystem::path> smokePaths{};
    for (int index = 1; index < argc; ++index) {
        if (std::string_view(argv[index]) == "--smoke-test") {
            smokeTest = true;
        } else if (smokeTest && std::string_view(argv[index]).front() != '-') {
            smokePaths.emplace_back(argv[index]);
        }
    }

    QApplication application(argc, argv);
    QApplication::setOrganizationName("jahorta");
    QApplication::setApplicationName("SpiceRack");
    QApplication::setApplicationDisplayName("SpiceRack");

    SpiceRackMainWindow window;
    window.show();

    if (smokeTest) {
        if (!smokePaths.empty()) {
            QTimer::singleShot(0, &window, [&window, &application, paths = std::move(smokePaths)]() {
                window.openDocumentBatch(paths,
                    [&window, &application](const bool success) {
                        application.exit(success && window.runSmokeChecks() ? 0 : 1);
                    }, false);
            });
        } else {
            QTimer::singleShot(0, &window, [&window, &application]() {
                application.exit(window.runSmokeChecks() ? 0 : 1);
            });
        }
    }
    return application.exec();
}
