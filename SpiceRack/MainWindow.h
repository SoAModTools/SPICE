#pragma once

#include "TaskController.h"

#include <QtCore/QSet>
#include <QtCore/QString>
#include <QtCore/QStringList>
#include <QtWidgets/QMainWindow>

#include <deque>
#include <filesystem>
#include <functional>
#include <vector>

class DocumentWorkbench;
class QAction;
class QDragEnterEvent;
class QDragLeaveEvent;
class QDragMoveEvent;
class QDropEvent;
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
    void openDocumentBatch(const std::vector<std::filesystem::path>& paths,
        std::function<void(bool)> completed = {}, bool showSummary = true);
    [[nodiscard]] bool runSmokeChecks();

protected:
    void closeEvent(QCloseEvent* event) override;
    void dragEnterEvent(QDragEnterEvent* event) override;
    void dragLeaveEvent(QDragLeaveEvent* event) override;
    void dragMoveEvent(QDragMoveEvent* event) override;
    void dropEvent(QDropEvent* event) override;

private:
    struct DocumentOpenOutcome {
        bool success = false;
        bool busy = false;
        QString message{};
        QStringList details{};
    };

    void chooseOpenDocument();
    void chooseNewGvr();
    void chooseNewPvr();
    void addWorkbench(DocumentWorkbench* workbench);
    void refreshTabTitle(DocumentWorkbench* workbench);
    void refreshDocumentActions();
    void closeTab(int index);
    [[nodiscard]] DocumentWorkbench* currentWorkbench() const;
    [[nodiscard]] int existingDocumentIndex(const std::filesystem::path& path) const;
    void openDocumentDetailed(const std::filesystem::path& path,
        std::function<void(DocumentOpenOutcome)> completed);
    void beginDroppedDocumentBatch(std::deque<std::filesystem::path> paths, QStringList issues,
        std::function<void(bool)> completed = {}, bool showSummary = true);
    void openNextDroppedDocument();
    void finishDroppedDocumentBatch();
    void setDocumentDropHighlight(bool active);
    void setEventsExpanded(bool expanded);
    void emphasizeEvents(spice::mix::EventLevel level);

    RackTaskController tasks_{};
    QTabWidget* tabs_ = nullptr;
    QListWidget* events_ = nullptr;
    QWidget* eventPanel_ = nullptr;
    QPushButton* cancelJob_ = nullptr;
    QToolButton* eventsToggle_ = nullptr;
    QAction* saveAsAction_ = nullptr;
    int eventAttention_ = 0;
    std::deque<std::filesystem::path> droppedDocuments_{};
    QStringList droppedDocumentIssues_{};
    int droppedDocumentSuccesses_ = 0;
    bool droppedBatchActive_ = false;
    bool droppedBatchShowSummary_ = true;
    std::function<void(bool)> droppedBatchCompleted_{};
    QSet<DocumentWorkbench*> discardedForWindowClose_{};
};
