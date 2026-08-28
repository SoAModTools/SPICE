#include "MainWindow.h"

#include <QtGui/QAction>
#include <QtGui/QFont>
#include <QtWidgets/QLabel>
#include <QtWidgets/QMenuBar>
#include <QtWidgets/QMessageBox>
#include <QtWidgets/QStatusBar>
#include <QtWidgets/QVBoxLayout>
#include <QtWidgets/QWidget>

SpiceRackMainWindow::SpiceRackMainWindow(QWidget* parent)
    : QMainWindow(parent) {
    setWindowTitle("SpiceRack");
    resize(1100, 720);

    auto* fileMenu = menuBar()->addMenu("&File");
    auto* exitAction = fileMenu->addAction("E&xit");
    connect(exitAction, &QAction::triggered, this, &QWidget::close);

    auto* helpMenu = menuBar()->addMenu("&Help");
    auto* aboutAction = helpMenu->addAction("&About SpiceRack");
    connect(aboutAction, &QAction::triggered, this, [this]() {
        QMessageBox::about(
            this,
            "About SpiceRack",
            "SpiceRack is the visual frontend for SPICE file operations.");
    });

    auto* workspace = new QWidget(this);
    auto* layout = new QVBoxLayout(workspace);
    layout->setContentsMargins(48, 48, 48, 48);
    layout->setSpacing(12);

    auto* title = new QLabel("SpiceRack", workspace);
    QFont titleFont = title->font();
    titleFont.setPointSize(titleFont.pointSize() + 8);
    titleFont.setBold(true);
    title->setFont(titleFont);

    auto* description = new QLabel(
        "The shared SpiceMix operation layer is ready. Interactive file workflows "
        "will be added to this workspace next.",
        workspace);
    description->setWordWrap(true);

    layout->addWidget(title);
    layout->addWidget(description);
    layout->addStretch();
    setCentralWidget(workspace);
    statusBar()->showMessage("Ready");
}
