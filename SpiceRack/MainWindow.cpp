#include "MainWindow.h"

#include "DocumentWorkbenches.h"

#include <QtCore/QPointer>
#include <QtGui/QAction>
#include <QtGui/QCloseEvent>
#include <QtGui/QKeySequence>
#include <QtWidgets/QDockWidget>
#include <QtWidgets/QFileDialog>
#include <QtWidgets/QHBoxLayout>
#include <QtWidgets/QLabel>
#include <QtWidgets/QListWidget>
#include <QtWidgets/QMenuBar>
#include <QtWidgets/QMessageBox>
#include <QtWidgets/QPushButton>
#include <QtWidgets/QStatusBar>
#include <QtWidgets/QTabBar>
#include <QtWidgets/QTabWidget>
#include <QtWidgets/QVBoxLayout>

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

} // namespace

SpiceRackMainWindow::SpiceRackMainWindow(QWidget* parent)
    : QMainWindow(parent), tasks_(this) {
    setWindowTitle("SpiceRack");
    resize(1280, 820);

    auto* fileMenu = menuBar()->addMenu("&File");
    auto* openAction = fileMenu->addAction("&Open...");
    openAction->setShortcut(QKeySequence::Open);
    connect(openAction, &QAction::triggered, this, [this]() { chooseOpenDocument(); });
    auto* newGvrAction = fileMenu->addAction("New &GVR from PNG...");
    connect(newGvrAction, &QAction::triggered, this, [this]() { chooseNewGvr(); });
    fileMenu->addSeparator();
    auto* saveAsAction = fileMenu->addAction("Save &As...");
    saveAsAction->setShortcut(QKeySequence::SaveAs);
    connect(saveAsAction, &QAction::triggered, this, [this]() {
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
            "SpiceRack is the visual frontend for inspecting MLD files and editing GVR textures.\n\n"
            "Document operations are provided by the shared, non-Qt SpiceMix layer.");
    });

    tabs_ = new QTabWidget(this);
    tabs_->setTabsClosable(true);
    tabs_->setMovable(true);
    tabs_->setDocumentMode(true);
    connect(tabs_, &QTabWidget::tabCloseRequested, this, [this](const int index) { closeTab(index); });
    auto* welcome = new QLabel(
        "<h2>SpiceRack</h2><p>Open an MLD or GVR file, or create a new GVR from a PNG.</p>"
        "<p>MLD texture changes are staged until <b>Save As</b>; the original MLD is never overwritten.</p>", tabs_);
    welcome->setAlignment(Qt::AlignCenter);
    welcome->setWordWrap(true);
    tabs_->addTab(welcome, "Welcome");
    tabs_->tabBar()->setTabButton(0, QTabBar::RightSide, nullptr);
    setCentralWidget(tabs_);

    auto* dock = new QDockWidget("Jobs and events", this);
    dock->setObjectName("jobsAndEventsDock");
    auto* dockBody = new QWidget(dock);
    auto* dockLayout = new QVBoxLayout(dockBody);
    auto* jobRow = new QHBoxLayout();
    jobStatus_ = new QLabel("Idle", dockBody);
    cancelJob_ = new QPushButton("Cancel", dockBody);
    cancelJob_->setEnabled(false);
    connect(cancelJob_, &QPushButton::clicked, this, [this]() { tasks_.cancel(); });
    jobRow->addWidget(jobStatus_);
    jobRow->addStretch();
    jobRow->addWidget(cancelJob_);
    events_ = new QListWidget(dockBody);
    dockLayout->addLayout(jobRow);
    dockLayout->addWidget(events_);
    dock->setWidget(dockBody);
    addDockWidget(Qt::BottomDockWidgetArea, dock);

    tasks_.setEventSink([this](const spice::mix::OperationEvent& event) {
        const QString prefix = event.level == spice::mix::EventLevel::Error ? "Error: "
            : event.level == spice::mix::EventLevel::Warning ? "Warning: "
            : event.level == spice::mix::EventLevel::Progress ? "Working: " : "";
        events_->addItem(prefix + QString::fromStdString(event.message));
        events_->scrollToBottom();
    });
    tasks_.setBusySink([this](const bool busy, const std::string& label) {
        jobStatus_->setText(busy ? QString::fromStdString(label) : "Idle");
        cancelJob_->setEnabled(busy);
        tabs_->setEnabled(!busy);
        statusBar()->showMessage(busy ? QString::fromStdString(label) : "Ready");
    });
    statusBar()->showMessage("Ready");
}

void SpiceRackMainWindow::chooseOpenDocument() {
    const auto path = QFileDialog::getOpenFileName(this, "Open SPICE document", {},
        "Supported files (*.mld *.gvr);;MLD files (*.mld);;GVR files (*.gvr);;All files (*)");
    if (!path.isEmpty()) openDocument(fspath(path));
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
    const int existing = existingDocumentIndex(path);
    if (existing >= 0) {
        tabs_->setCurrentIndex(existing);
        if (completed) completed(true);
        return;
    }
    const auto extension = QString::fromStdWString(path.extension().wstring()).toLower();
    if (extension != ".mld" && extension != ".gvr") {
        if (showErrors) QMessageBox::critical(this, "SpiceRack", "This workbench currently opens .mld and .gvr files.");
        if (completed) completed(false);
        return;
    }
    struct OpenState {
        spice::mix::MldDocumentSession::OpenResult mld{};
        spice::mix::GvrDocumentSession::OpenResult gvr{};
    };
    auto state = std::make_shared<OpenState>();
    QPointer<SpiceRackMainWindow> self(this);
    const bool started = tasks_.run("Open " + path.filename().string(),
        [path, extension, state](const auto& context) {
            if (extension == ".mld") state->mld = spice::mix::MldDocumentSession::open(path, context);
            else state->gvr = spice::mix::GvrDocumentSession::open(path, context);
        }, [self, extension, state, completed = std::move(completed), showErrors]() mutable {
            if (!self) return;
            bool success = false;
            if (extension == ".mld" && state->mld.result.ok() && state->mld.session) {
                self->addWorkbench(new MldWorkbench(state->mld.session, self->tasks_, self));
                success = true;
            } else if (extension == ".gvr" && state->gvr.result.ok() && state->gvr.session) {
                self->addWorkbench(new GvrWorkbench(state->gvr.session, self->tasks_, self));
                success = true;
            } else {
                const auto& result = extension == ".mld" ? state->mld.result : state->gvr.result;
                if (showErrors) QMessageBox::critical(self, "SpiceRack", QString::fromStdString(result.message));
            }
            if (completed) completed(success);
        });
    if (!started) {
        if (showErrors) QMessageBox::information(this, "SpiceRack", "Finish or cancel the current job first.");
        if (completed) completed(false);
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
}

void SpiceRackMainWindow::refreshTabTitle(DocumentWorkbench* workbench) {
    const int index = tabs_->indexOf(workbench);
    if (index >= 0) tabs_->setTabText(index, workbench->displayName() + (workbench->dirty() ? " *" : ""));
}

DocumentWorkbench* SpiceRackMainWindow::currentWorkbench() const {
    return dynamic_cast<DocumentWorkbench*>(tabs_->currentWidget());
}

int SpiceRackMainWindow::existingDocumentIndex(const std::filesystem::path& path) const {
    const auto wanted = normalizedPath(path);
    for (int index = 0; index < tabs_->count(); ++index) {
        const auto* workbench = dynamic_cast<DocumentWorkbench*>(tabs_->widget(index));
        if (workbench && workbench->sourcePath().has_value()
            && normalizedPath(*workbench->sourcePath()) == wanted) return index;
    }
    return -1;
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
