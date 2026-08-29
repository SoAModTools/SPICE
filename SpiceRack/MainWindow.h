#pragma once

#include "TaskController.h"

#include <QtCore/QSet>
#include <QtWidgets/QMainWindow>

#include <filesystem>
#include <functional>

class DocumentWorkbench;
class QLabel;
class QListWidget;
class QPushButton;
class QTabWidget;
class QToolButton;

class SpiceRackMainWindow final : public QMainWindow {
public:
    explicit SpiceRackMainWindow(QWidget* parent = nullptr);

    void openDocument(const std::filesystem::path& path,
        std::function<void(bool)> completed = {}, bool showErrors = true);
    [[nodiscard]] bool runSmokeChecks();

protected:
    void closeEvent(QCloseEvent* event) override;

private:
    void chooseOpenDocument();
    void chooseNewGvr();
    void chooseNewPvr();
    void addWorkbench(DocumentWorkbench* workbench);
    void refreshTabTitle(DocumentWorkbench* workbench);
    void closeTab(int index);
    [[nodiscard]] DocumentWorkbench* currentWorkbench() const;
    [[nodiscard]] int existingDocumentIndex(const std::filesystem::path& path) const;
    void setEventsExpanded(bool expanded);
    void emphasizeEvents(spice::mix::EventLevel level);

    RackTaskController tasks_{};
    QTabWidget* tabs_ = nullptr;
    QListWidget* events_ = nullptr;
    QWidget* eventPanel_ = nullptr;
    QPushButton* cancelJob_ = nullptr;
    QToolButton* eventsToggle_ = nullptr;
    int eventAttention_ = 0;
    QSet<DocumentWorkbench*> discardedForWindowClose_{};
};
