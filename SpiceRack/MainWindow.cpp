#include "MainWindow.h"

#include "DocumentWorkbenches.h"
#include "TextureViewport.h"

#include <QtCore/QPointer>
#include <QtCore/QMimeData>
#include <QtCore/QTimer>
#include <QtCore/QUrl>
#include <QtGui/QAction>
#include <QtGui/QCloseEvent>
#include <QtGui/QDragEnterEvent>
#include <QtGui/QDragLeaveEvent>
#include <QtGui/QDragMoveEvent>
#include <QtGui/QDropEvent>
#include <QtGui/QKeySequence>
#include <QtWidgets/QFileDialog>
#include <QtWidgets/QFrame>
#include <QtWidgets/QLabel>
#include <QtWidgets/QListWidget>
#include <QtWidgets/QMenuBar>
#include <QtWidgets/QMessageBox>
#include <QtWidgets/QPushButton>
#include <QtWidgets/QStatusBar>
#include <QtWidgets/QStyle>
#include <QtWidgets/QTabBar>
#include <QtWidgets/QTabWidget>
#include <QtWidgets/QToolButton>
#include <QtWidgets/QVBoxLayout>

#include <algorithm>
#include <deque>
#include <memory>
#include <system_error>

namespace {

std::filesystem::path fspath(const QString& path) {
    return std::filesystem::path(path.toStdWString());
}

std::filesystem::path normalizedPath(const std::filesystem::path& path) {
    std::error_code error{};
    const auto canonical = std::filesystem::weakly_canonical(path, error);
    return error ? path.lexically_normal() : canonical;
}

QString normalizedPathKey(const std::filesystem::path& path) {
    return QString::fromStdWString(normalizedPath(path).wstring()).toCaseFolded();
}

bool supportedDocumentPath(const std::filesystem::path& path) {
    const auto extension = QString::fromStdWString(path.extension().wstring()).toLower();
    return extension == ".mld" || extension == ".gvr" || extension == ".pvr"
        || extension == ".sml" || extension == ".sst" || extension == ".ect";
}

QString logicalDocumentKey(const std::filesystem::path& path) {
    const auto extension = QString::fromStdWString(path.extension().wstring()).toLower();
    if (extension == ".sml" || extension == ".sst") {
        const auto parent = normalizedPath(path.has_parent_path() ? path.parent_path() : std::filesystem::current_path());
        return (QString::fromStdWString(parent.wstring()) + "/"
            + QString::fromStdWString(path.stem().wstring()) + ".sst-sml").toCaseFolded();
    }
    return normalizedPathKey(path);
}

struct DocumentDropClassification {
    std::deque<std::filesystem::path> paths{};
    QStringList issues{};
};

DocumentDropClassification classifyDocumentDrop(const QMimeData* mimeData) {
    DocumentDropClassification result{};
    if (!mimeData || !mimeData->hasUrls()) return result;
    QSet<QString> seen{};
    for (const auto& url : mimeData->urls()) {
        if (!url.isLocalFile()) {
            result.issues.push_back(QString("Unsupported non-local URL: %1").arg(url.toDisplayString()));
            continue;
        }
        const auto local = url.toLocalFile();
        const std::filesystem::path path(local.toStdWString());
        std::error_code error{};
        if (std::filesystem::is_directory(path, error) && !error) {
            result.issues.push_back(QString("Directories are not supported: %1").arg(local));
            continue;
        }
        if (!supportedDocumentPath(path)) {
            result.issues.push_back(QString("Unsupported file type: %1").arg(local));
            continue;
        }
        const auto key = logicalDocumentKey(path);
        if (seen.contains(key)) continue;
        seen.insert(key);
        result.paths.push_back(path);
    }
    return result;
}

} // namespace

SpiceRackMainWindow::SpiceRackMainWindow(QWidget* parent)
    : QMainWindow(parent), tasks_(this) {
    setWindowTitle("SpiceRack");
    resize(1280, 820);
    setAcceptDrops(true);

    auto* fileMenu = menuBar()->addMenu("&File");
    auto* openAction = fileMenu->addAction("&Open...");
    openAction->setShortcut(QKeySequence::Open);
    connect(openAction, &QAction::triggered, this, [this]() { chooseOpenDocument(); });
    auto* newGvrAction = fileMenu->addAction("New &GVR from PNG...");
    connect(newGvrAction, &QAction::triggered, this, [this]() { chooseNewGvr(); });
    auto* newPvrAction = fileMenu->addAction("New &PVR from PNG...");
    connect(newPvrAction, &QAction::triggered, this, [this]() { chooseNewPvr(); });
    fileMenu->addSeparator();
    saveAsAction_ = fileMenu->addAction("Save &As...");
    saveAsAction_->setShortcut(QKeySequence::SaveAs);
    connect(saveAsAction_, &QAction::triggered, this, [this]() {
        if (auto* workbench = currentWorkbench()) workbench->requestSaveAs();
    });
    auto* closeAction = fileMenu->addAction("&Close document");
    closeAction->setShortcut(QKeySequence::Close);
    connect(closeAction, &QAction::triggered, this, [this]() { closeTab(tabs_->currentIndex()); });
    fileMenu->addSeparator();
    auto* exitAction = fileMenu->addAction("E&xit");
    connect(exitAction, &QAction::triggered, this, &QWidget::close);

    auto* helpMenu = menuBar()->addMenu("&Help");
    auto* aboutAction = helpMenu->addAction("&About SpiceRack");
    connect(aboutAction, &QAction::triggered, this, [this]() {
        QMessageBox::about(this, "About SpiceRack",
            "SpiceRack is the visual frontend for inspecting MLD, ECT, and paired SST/SML files "
            "and editing GVR and PVR textures.\n\n"
            "Document operations are provided by the shared, non-Qt SpiceMix layer.");
    });

    tabs_ = new QTabWidget(this);
    tabs_->setTabsClosable(true);
    tabs_->setMovable(true);
    tabs_->setDocumentMode(true);
    connect(tabs_, &QTabWidget::tabCloseRequested, this, [this](const int index) { closeTab(index); });
    connect(tabs_, &QTabWidget::currentChanged, this, [this]() { refreshDocumentActions(); });
    auto* welcome = new QLabel(
        "<h2>SpiceRack</h2><p>Open an MLD, ECT, paired SST/SML, GVR, or PVR document, "
        "or create a new texture from a PNG.</p>"
        "<p>MLD texture changes are staged until <b>Save As</b>; the original MLD is never overwritten.</p>", tabs_);
    welcome->setAlignment(Qt::AlignCenter);
    welcome->setWordWrap(true);
    tabs_->addTab(welcome, "Welcome");
    tabs_->tabBar()->setTabButton(0, QTabBar::RightSide, nullptr);
    auto* central = new QWidget(this);
    central->setObjectName("rackCentralWidget");
    central->setProperty("documentDropActive", false);
    central->setStyleSheet(
        "QWidget#rackCentralWidget[documentDropActive=\"true\"] { border: 3px dashed #64beff; }");
    auto* centralLayout = new QVBoxLayout(central);
    centralLayout->setContentsMargins(0, 0, 0, 0);
    centralLayout->setSpacing(0);
    centralLayout->addWidget(tabs_, 1);

    eventPanel_ = new QFrame(central);
    eventPanel_->setObjectName("eventsPanel");
    eventPanel_->setFixedHeight(200);
    auto* eventLayout = new QVBoxLayout(eventPanel_);
    eventLayout->setContentsMargins(4, 4, 4, 4);
    events_ = new QListWidget(eventPanel_);
    eventLayout->addWidget(events_);
    centralLayout->addWidget(eventPanel_);
    eventPanel_->hide();
    setCentralWidget(central);

    cancelJob_ = new QPushButton("Cancel", this);
    cancelJob_->setVisible(false);
    connect(cancelJob_, &QPushButton::clicked, this, [this]() { tasks_.cancel(); });
    eventsToggle_ = new QToolButton(this);
    eventsToggle_->setObjectName("eventsToggle");
    eventsToggle_->setArrowType(Qt::UpArrow);
    eventsToggle_->setToolTip("Show jobs and events");
    eventsToggle_->setAutoRaise(true);
    connect(eventsToggle_, &QToolButton::clicked, this,
        [this]() { setEventsExpanded(eventPanel_->isHidden()); });
    statusBar()->addPermanentWidget(cancelJob_);
    statusBar()->addPermanentWidget(eventsToggle_);

    tasks_.setEventSink([this](const spice::mix::OperationEvent& event) {
        const QString prefix = event.level == spice::mix::EventLevel::Error ? "Error: "
            : event.level == spice::mix::EventLevel::Warning ? "Warning: "
            : event.level == spice::mix::EventLevel::Progress ? "Working: " : "";
        events_->addItem(prefix + QString::fromStdString(event.message));
        events_->scrollToBottom();
        if (event.level == spice::mix::EventLevel::Warning
            || event.level == spice::mix::EventLevel::Error) {
            emphasizeEvents(event.level);
        }
    });
    tasks_.setBusySink([this](const bool busy, const std::string& label) {
        cancelJob_->setVisible(busy);
        tabs_->setEnabled(!busy);
        statusBar()->showMessage(busy ? QString::fromStdString(label) : "Ready");
    });
    statusBar()->showMessage("Ready");
    refreshDocumentActions();
}

void SpiceRackMainWindow::setEventsExpanded(const bool expanded) {
    eventPanel_->setVisible(expanded);
    eventsToggle_->setArrowType(expanded ? Qt::DownArrow : Qt::UpArrow);
    eventsToggle_->setToolTip(expanded ? "Hide jobs and events" : "Show jobs and events");
    if (expanded) {
        eventAttention_ = 0;
        eventsToggle_->setStyleSheet({});
        events_->scrollToBottom();
    }
}

void SpiceRackMainWindow::emphasizeEvents(const spice::mix::EventLevel level) {
    if (!eventPanel_->isHidden()) return;
    const int attention = level == spice::mix::EventLevel::Error ? 2 : 1;
    eventAttention_ = std::max(eventAttention_, attention);
    eventsToggle_->setStyleSheet(eventAttention_ == 2
        ? "QToolButton { color: #ff6b6b; font-weight: bold; }"
        : "QToolButton { color: #e6b450; font-weight: bold; }");
}

bool SpiceRackMainWindow::runSmokeChecks() {
    const bool startsCollapsed = eventPanel_->isHidden();
    setEventsExpanded(true);
    const bool expanded = !eventPanel_->isHidden() && eventsToggle_->arrowType() == Qt::DownArrow;
    setEventsExpanded(false);
    const bool collapsed = eventPanel_->isHidden() && eventsToggle_->arrowType() == Qt::UpArrow;
    const bool rendering = TextureViewport::runRenderingSmokeChecks();
    QMimeData dropMime{};
    dropMime.setUrls({ QUrl::fromLocalFile("C:/rack-drop/one.MLD"),
        QUrl::fromLocalFile("C:/rack-drop/two.gVr"),
        QUrl::fromLocalFile("C:/RACK-DROP/ONE.mld"),
        QUrl::fromLocalFile("C:/rack-drop/s006.SML"),
        QUrl::fromLocalFile("C:/rack-drop/S006.sst"),
        QUrl::fromLocalFile("C:/rack-drop/a099a.ECT"),
        QUrl::fromLocalFile("C:/rack-drop/replacement.png"),
        QUrl("https://example.invalid/remote.pvr") });
    const auto classified = classifyDocumentDrop(&dropMime);
    const bool dropRouting = classified.paths.size() == 4
        && classified.issues.size() == 2
        && supportedDocumentPath("upper.PVR")
        && supportedDocumentPath("stage.SML")
        && supportedDocumentPath("encounters.ECT")
        && !supportedDocumentPath("replacement.png")
        && !centralWidget()->property("documentDropActive").toBool();
    auto* workbench = currentWorkbench();
    return startsCollapsed && expanded && collapsed && rendering && dropRouting
        && saveAsAction_->isEnabled() == (workbench && workbench->canSaveAs())
        && (!workbench || workbench->runSmokeChecks());
}

void SpiceRackMainWindow::chooseOpenDocument() {
    const auto path = QFileDialog::getOpenFileName(this, "Open SPICE document", {},
        "Supported files (*.mld *.ect *.sml *.sst *.gvr *.pvr);;MLD files (*.mld);;ECT files (*.ect);;"
        "SST/SML pairs (*.sml *.sst);;GVR files (*.gvr);;PVR files (*.pvr);;All files (*)");
    if (!path.isEmpty()) openDocument(fspath(path));
}

void SpiceRackMainWindow::chooseNewPvr() {
    const auto path = QFileDialog::getOpenFileName(this, "Create PVR from PNG", {}, "PNG images (*.png)");
    if (path.isEmpty()) return;
    auto result = std::make_shared<spice::mix::PvrDocumentSession::OpenResult>();
    QPointer<SpiceRackMainWindow> self(this);
    if (!tasks_.run("Create PVR document", [path = fspath(path), result](const auto& context) {
        *result = spice::mix::PvrDocumentSession::createFromPng(path, context);
    }, [self, result]() {
        if (!self) return;
        if (!result->result.ok() || !result->session) {
            QMessageBox::critical(self, "SpiceRack", QString::fromStdString(result->result.message));
            return;
        }
        self->addWorkbench(new PvrWorkbench(result->session, self->tasks_, self));
    })) {
        QMessageBox::information(this, "SpiceRack", "Finish or cancel the current job first.");
    }
}

void SpiceRackMainWindow::chooseNewGvr() {
    const auto path = QFileDialog::getOpenFileName(this, "Create GVR from PNG", {}, "PNG images (*.png)");
    if (path.isEmpty()) return;
    auto result = std::make_shared<spice::mix::GvrDocumentSession::OpenResult>();
    QPointer<SpiceRackMainWindow> self(this);
    if (!tasks_.run("Create GVR document", [path = fspath(path), result](const auto& context) {
        *result = spice::mix::GvrDocumentSession::createFromPng(path, context);
    }, [self, result]() {
        if (!self) return;
        if (!result->result.ok() || !result->session) {
            QMessageBox::critical(self, "SpiceRack", QString::fromStdString(result->result.message));
            return;
        }
        self->addWorkbench(new GvrWorkbench(result->session, self->tasks_, self));
    })) {
        QMessageBox::information(this, "SpiceRack", "Finish or cancel the current job first.");
    }
}

void SpiceRackMainWindow::openDocument(const std::filesystem::path& path,
    std::function<void(bool)> completed, const bool showErrors) {
    openDocumentDetailed(path,
        [this, completed = std::move(completed), showErrors](DocumentOpenOutcome outcome) mutable {
            if (!outcome.success && showErrors) {
                if (outcome.busy) QMessageBox::information(this, "SpiceRack", outcome.message);
                else {
                    QMessageBox error(QMessageBox::Critical, "SpiceRack", outcome.message,
                        QMessageBox::Ok, this);
                    if (!outcome.details.isEmpty()) error.setDetailedText(outcome.details.join('\n'));
                    error.exec();
                }
            }
            if (completed) completed(outcome.success);
        });
}

void SpiceRackMainWindow::openDocumentBatch(const std::vector<std::filesystem::path>& paths,
    std::function<void(bool)> completed, const bool showSummary) {
    std::deque<std::filesystem::path> accepted{};
    QStringList issues{};
    QSet<QString> seen{};
    for (const auto& path : paths) {
        std::error_code error{};
        if (std::filesystem::is_directory(path, error) && !error) {
            issues.push_back(QString("Directories are not supported: %1")
                .arg(QString::fromStdWString(path.wstring())));
            continue;
        }
        if (!supportedDocumentPath(path)) {
            issues.push_back(QString("Unsupported file type: %1")
                .arg(QString::fromStdWString(path.wstring())));
            continue;
        }
        const auto key = logicalDocumentKey(path);
        if (seen.contains(key)) continue;
        seen.insert(key);
        accepted.push_back(path);
    }
    beginDroppedDocumentBatch(std::move(accepted), std::move(issues),
        std::move(completed), showSummary);
}

void SpiceRackMainWindow::openDocumentDetailed(const std::filesystem::path& path,
    std::function<void(DocumentOpenOutcome)> completed) {
    const int existing = existingDocumentIndex(path);
    if (existing >= 0) {
        tabs_->setCurrentIndex(existing);
        if (completed) completed({ .success = true, .message = "Document was already open." });
        return;
    }
    const auto extension = QString::fromStdWString(path.extension().wstring()).toLower();
    if (extension != ".mld" && extension != ".gvr" && extension != ".pvr"
        && extension != ".sml" && extension != ".sst" && extension != ".ect") {
        if (completed) completed({ .message = "This workbench opens .mld, .ect, paired .sml/.sst, .gvr, and .pvr files." });
        return;
    }
    struct OpenState {
        spice::mix::MldDocumentSession::OpenResult mld{};
        spice::mix::GvrDocumentSession::OpenResult gvr{};
        spice::mix::PvrDocumentSession::OpenResult pvr{};
        spice::mix::SstSmlDocumentSession::OpenResult sstSml{};
        spice::mix::EctDocumentSession::OpenResult ect{};
    };
    auto state = std::make_shared<OpenState>();
    auto completion = std::make_shared<std::function<void(DocumentOpenOutcome)>>(std::move(completed));
    QPointer<SpiceRackMainWindow> self(this);
    const bool started = tasks_.run("Open " + path.filename().string(),
        [path, extension, state](const auto& context) {
            if (extension == ".mld") state->mld = spice::mix::MldDocumentSession::open(path, context);
            else if (extension == ".gvr") state->gvr = spice::mix::GvrDocumentSession::open(path, context);
            else if (extension == ".pvr") state->pvr = spice::mix::PvrDocumentSession::open(path, context);
            else if (extension == ".ect") state->ect = spice::mix::EctDocumentSession::open(path, context);
            else state->sstSml = spice::mix::SstSmlDocumentSession::open(path, context);
        }, [self, extension, state, completion]() mutable {
            if (!self) return;
            DocumentOpenOutcome outcome{};
            if (extension == ".mld" && state->mld.result.ok() && state->mld.session) {
                self->addWorkbench(new MldWorkbench(state->mld.session, self->tasks_, self));
                outcome.success = true;
            } else if (extension == ".gvr" && state->gvr.result.ok() && state->gvr.session) {
                self->addWorkbench(new GvrWorkbench(state->gvr.session, self->tasks_, self));
                outcome.success = true;
            } else if (extension == ".pvr" && state->pvr.result.ok() && state->pvr.session) {
                self->addWorkbench(new PvrWorkbench(state->pvr.session, self->tasks_, self));
                outcome.success = true;
            } else if ((extension == ".sml" || extension == ".sst")
                && state->sstSml.result.ok() && state->sstSml.session) {
                self->addWorkbench(new SstSmlWorkbench(state->sstSml.session, self));
                outcome.success = true;
            } else if (extension == ".ect" && state->ect.result.ok() && state->ect.session) {
                self->addWorkbench(new EctWorkbench(state->ect.session, self));
                outcome.success = true;
            } else {
                const auto& result = extension == ".mld" ? state->mld.result
                    : extension == ".gvr" ? state->gvr.result
                    : extension == ".pvr" ? state->pvr.result
                    : extension == ".ect" ? state->ect.result : state->sstSml.result;
                outcome.message = QString::fromStdString(result.message);
                for (const auto& diagnostic : result.diagnostics) {
                    outcome.details.push_back(QString::fromStdString(diagnostic));
                }
            }
            if (outcome.success) outcome.message = "Opened document.";
            if (*completion) (*completion)(std::move(outcome));
        });
    if (!started) {
        if (*completion) (*completion)({ .busy = true,
            .message = "Finish or cancel the current job first." });
    }
}

void SpiceRackMainWindow::addWorkbench(DocumentWorkbench* workbench) {
    if (tabs_->count() == 1 && qobject_cast<QLabel*>(tabs_->widget(0))) {
        auto* welcome = tabs_->widget(0);
        tabs_->removeTab(0);
        welcome->deleteLater();
    }
    const int index = tabs_->addTab(workbench, workbench->displayName());
    tabs_->setCurrentIndex(index);
    workbench->setStateChanged([this, workbench]() { refreshTabTitle(workbench); });
    refreshTabTitle(workbench);
    refreshDocumentActions();
}

void SpiceRackMainWindow::refreshTabTitle(DocumentWorkbench* workbench) {
    const int index = tabs_->indexOf(workbench);
    if (index >= 0) tabs_->setTabText(index, workbench->displayName() + (workbench->dirty() ? " *" : ""));
}

void SpiceRackMainWindow::refreshDocumentActions() {
    const auto* workbench = currentWorkbench();
    if (saveAsAction_) saveAsAction_->setEnabled(workbench && workbench->canSaveAs());
}

DocumentWorkbench* SpiceRackMainWindow::currentWorkbench() const {
    return dynamic_cast<DocumentWorkbench*>(tabs_->currentWidget());
}

int SpiceRackMainWindow::existingDocumentIndex(const std::filesystem::path& path) const {
    const auto wanted = normalizedPathKey(path);
    for (int index = 0; index < tabs_->count(); ++index) {
        const auto* workbench = dynamic_cast<DocumentWorkbench*>(tabs_->widget(index));
        if (!workbench) continue;
        for (const auto& sourcePath : workbench->sourcePaths()) {
            if (normalizedPathKey(sourcePath) == wanted) return index;
        }
    }
    return -1;
}

void SpiceRackMainWindow::beginDroppedDocumentBatch(
    std::deque<std::filesystem::path> paths, QStringList issues,
    std::function<void(bool)> completed, const bool showSummary) {
    if (tasks_.busy() || droppedBatchActive_) {
        statusBar()->showMessage("Finish or cancel the current job before dropping documents.", 4000);
        if (completed) completed(false);
        return;
    }
    droppedDocuments_ = std::move(paths);
    droppedDocumentIssues_ = std::move(issues);
    droppedDocumentSuccesses_ = 0;
    droppedBatchActive_ = true;
    droppedBatchShowSummary_ = showSummary;
    droppedBatchCompleted_ = std::move(completed);
    openNextDroppedDocument();
}

void SpiceRackMainWindow::openNextDroppedDocument() {
    if (droppedDocuments_.empty()) {
        finishDroppedDocumentBatch();
        return;
    }
    const auto path = std::move(droppedDocuments_.front());
    droppedDocuments_.pop_front();
    openDocumentDetailed(path, [this, path](DocumentOpenOutcome outcome) {
        if (outcome.success) {
            ++droppedDocumentSuccesses_;
        } else {
            QString issue = QString("%1: %2")
                .arg(QString::fromStdWString(path.filename().wstring()), outcome.message);
            if (!outcome.details.isEmpty()) issue += "\n  " + outcome.details.join("\n  ");
            droppedDocumentIssues_.push_back(std::move(issue));
        }
        QTimer::singleShot(0, this, [this]() { openNextDroppedDocument(); });
    });
}

void SpiceRackMainWindow::finishDroppedDocumentBatch() {
    droppedBatchActive_ = false;
    const int issues = droppedDocumentIssues_.size();
    const bool success = issues == 0 && droppedDocumentSuccesses_ > 0;
    statusBar()->showMessage(QString("Opened or activated %1 document(s); %2 skipped or failed.")
        .arg(droppedDocumentSuccesses_).arg(issues), 6000);
    if (issues > 0 && droppedBatchShowSummary_) {
        const QString details = droppedDocumentIssues_.join('\n');
        events_->addItem("Warning: Drag-and-drop completed with skipped or failed files.");
        events_->scrollToBottom();
        emphasizeEvents(spice::mix::EventLevel::Warning);
        QMessageBox warning(QMessageBox::Warning, "Drag-and-drop results",
            QString("Opened or activated %1 document(s). %2 file(s) were skipped or failed.")
                .arg(droppedDocumentSuccesses_).arg(issues),
            QMessageBox::Ok, this);
        warning.setDetailedText(details);
        warning.exec();
    }
    droppedDocuments_.clear();
    droppedDocumentIssues_.clear();
    droppedDocumentSuccesses_ = 0;
    droppedBatchShowSummary_ = true;
    auto completed = std::move(droppedBatchCompleted_);
    if (completed) completed(success);
}

void SpiceRackMainWindow::setDocumentDropHighlight(const bool active) {
    auto* central = centralWidget();
    if (!central || central->property("documentDropActive").toBool() == active) return;
    central->setProperty("documentDropActive", active);
    central->style()->unpolish(central);
    central->style()->polish(central);
    central->update();
}

void SpiceRackMainWindow::dragEnterEvent(QDragEnterEvent* event) {
    const auto classified = classifyDocumentDrop(event->mimeData());
    if (classified.paths.empty()) {
        event->ignore();
        return;
    }
    if (tasks_.busy() || droppedBatchActive_) {
        statusBar()->showMessage("Finish or cancel the current job before dropping documents.", 4000);
        event->ignore();
        return;
    }
    setDocumentDropHighlight(true);
    statusBar()->showMessage(QString("Drop to open %1 document(s).").arg(classified.paths.size()));
    event->setDropAction(Qt::CopyAction);
    event->accept();
}

void SpiceRackMainWindow::dragMoveEvent(QDragMoveEvent* event) {
    const auto classified = classifyDocumentDrop(event->mimeData());
    if (classified.paths.empty() || tasks_.busy() || droppedBatchActive_) {
        setDocumentDropHighlight(false);
        event->ignore();
        return;
    }
    setDocumentDropHighlight(true);
    event->setDropAction(Qt::CopyAction);
    event->accept();
}

void SpiceRackMainWindow::dragLeaveEvent(QDragLeaveEvent* event) {
    setDocumentDropHighlight(false);
    if (!tasks_.busy() && !droppedBatchActive_) statusBar()->showMessage("Ready");
    event->accept();
}

void SpiceRackMainWindow::dropEvent(QDropEvent* event) {
    setDocumentDropHighlight(false);
    const auto classified = classifyDocumentDrop(event->mimeData());
    if (classified.paths.empty() || tasks_.busy() || droppedBatchActive_) {
        if (tasks_.busy() || droppedBatchActive_) {
            statusBar()->showMessage("Finish or cancel the current job before dropping documents.", 4000);
        }
        event->ignore();
        return;
    }
    event->setDropAction(Qt::CopyAction);
    event->accept();
    beginDroppedDocumentBatch(classified.paths, classified.issues);
}

void SpiceRackMainWindow::closeTab(const int index) {
    if (index < 0) return;
    auto* workbench = dynamic_cast<DocumentWorkbench*>(tabs_->widget(index));
    if (!workbench) return;
    if (tasks_.busy()) {
        QMessageBox::information(this, "SpiceRack", "Finish or cancel the current job before closing its document.");
        return;
    }
    if (workbench->dirty()) {
        const auto choice = QMessageBox::warning(this, "Unsaved changes",
            QString("Save changes to %1?").arg(workbench->displayName()),
            QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel, QMessageBox::Save);
        if (choice == QMessageBox::Cancel) return;
        if (choice == QMessageBox::Save) {
            QPointer<DocumentWorkbench> guarded(workbench);
            workbench->requestSaveAs([this, guarded](const bool saved) {
                if (saved && guarded) {
                    const int current = tabs_->indexOf(guarded);
                    if (current >= 0) closeTab(current);
                }
            });
            return;
        }
    }
    tabs_->removeTab(index);
    workbench->deleteLater();
    refreshDocumentActions();
}

void SpiceRackMainWindow::closeEvent(QCloseEvent* event) {
    if (tasks_.busy()) {
        const auto choice = QMessageBox::question(this, "Job in progress",
            "Cancel the current job and exit?", QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
        if (choice != QMessageBox::Yes) { event->ignore(); return; }
        tasks_.cancel();
        statusBar()->showMessage("Cancelling current job; close again when it is idle.");
        event->ignore();
        return;
    }
    for (int index = 0; index < tabs_->count(); ++index) {
        auto* workbench = dynamic_cast<DocumentWorkbench*>(tabs_->widget(index));
        if (!workbench || !workbench->dirty() || discardedForWindowClose_.contains(workbench)) continue;
        const auto choice = QMessageBox::warning(this, "Unsaved changes",
            QString("Save changes to %1 before exiting?").arg(workbench->displayName()),
            QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel, QMessageBox::Save);
        if (choice == QMessageBox::Cancel) { event->ignore(); return; }
        if (choice == QMessageBox::Discard) {
            discardedForWindowClose_.insert(workbench);
            --index;
            continue;
        }
        QPointer<SpiceRackMainWindow> self(this);
        workbench->requestSaveAs([self](const bool saved) { if (saved && self) self->close(); });
        event->ignore();
        return;
    }
    event->accept();
}
