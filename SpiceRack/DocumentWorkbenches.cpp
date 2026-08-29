#include "DocumentWorkbenches.h"
#include "TextureViewport.h"

#include <QtCore/QPointer>
#include <QtWidgets/QCheckBox>
#include <QtWidgets/QComboBox>
#include <QtWidgets/QFileDialog>
#include <QtWidgets/QFormLayout>
#include <QtWidgets/QGroupBox>
#include <QtWidgets/QHBoxLayout>
#include <QtWidgets/QHeaderView>
#include <QtWidgets/QLabel>
#include <QtWidgets/QMessageBox>
#include <QtWidgets/QPushButton>
#include <QtWidgets/QDoubleSpinBox>
#include <QtWidgets/QSplitter>
#include <QtWidgets/QTabWidget>
#include <QtWidgets/QTableWidget>
#include <QtWidgets/QTextEdit>
#include <QtWidgets/QVBoxLayout>

#include <memory>
#include <algorithm>
#include <utility>

namespace {

QString qpath(const std::filesystem::path& path) {
    return QString::fromStdWString(path.wstring());
}

std::filesystem::path fspath(const QString& path) {
    return std::filesystem::path(path.toStdWString());
}

std::filesystem::path mldBlenderIrFilename(const std::filesystem::path& source) {
    auto filename = source.stem();
    filename += ".json";
    return filename;
}

std::filesystem::path mldEntryListFilename(const std::filesystem::path& source) {
    auto filename = source.stem();
    filename += ".mld.entries.json";
    return filename;
}

void showResult(QWidget* parent, const spice::mix::DocumentResult& result) {
    if (result.status == spice::mix::OperationStatus::Failure) {
        QMessageBox::critical(parent, "SpiceRack", QString::fromStdString(result.message));
    } else if (result.status == spice::mix::OperationStatus::Cancelled) {
        QMessageBox::information(parent, "SpiceRack", result.message.empty()
            ? "The operation was cancelled." : QString::fromStdString(result.message));
    }
}

struct EncodingControls {
    QGroupBox* group = nullptr;
    QComboBox* format = nullptr;
    QComboBox* palette = nullptr;
    QComboBox* mipmaps = nullptr;
    QComboBox* globalMode = nullptr;
    QDoubleSpinBox* globalValue = nullptr;
    QComboBox* aklz = nullptr;

    spice::mix::GvrEncodingOverrides overrides() const {
        spice::mix::GvrEncodingOverrides out{};
        if (!group || !group->isChecked()) return out;
        const int formatValue = format->currentData().toInt();
        if (formatValue >= 0) out.format = static_cast<spice::mix::GvrTextureFormat>(formatValue);
        const int paletteValue = palette->currentData().toInt();
        if (paletteValue >= 0) out.paletteFormat = static_cast<spice::mix::GvrPaletteFormat>(paletteValue);
        const int mipmapValue = mipmaps->currentData().toInt();
        if (mipmapValue >= 0) out.mipmaps = mipmapValue != 0;
        const int global = globalMode->currentData().toInt();
        out.globalIndex.kind = static_cast<spice::mix::GvrGlobalIndexKind>(global);
        if (out.globalIndex.kind == spice::mix::GvrGlobalIndexKind::Value) {
            out.globalIndex.value = static_cast<std::uint32_t>(globalValue->value());
        }
        return out;
    }

    spice::mix::AklzPolicy aklzPolicy() const {
        if (!aklz || !group || !group->isChecked()) return spice::mix::AklzPolicy::Preserve;
        return static_cast<spice::mix::AklzPolicy>(aklz->currentData().toInt());
    }
};

EncodingControls addEncodingControls(QVBoxLayout* layout, const bool includeAklz) {
    EncodingControls controls{};
    controls.group = new QGroupBox("Advanced encoding", layout->parentWidget());
    controls.group->setCheckable(true);
    controls.group->setChecked(false);
    auto* form = new QFormLayout(controls.group);

    controls.format = new QComboBox(controls.group);
    controls.format->addItem("Preserve current", -1);
    const std::pair<const char*, spice::mix::GvrTextureFormat> formats[] = {
        { "I4", spice::mix::GvrTextureFormat::I4 }, { "I8", spice::mix::GvrTextureFormat::I8 },
        { "IA4", spice::mix::GvrTextureFormat::IA4 }, { "IA8", spice::mix::GvrTextureFormat::IA8 },
        { "RGB565", spice::mix::GvrTextureFormat::RGB565 }, { "RGB5A3", spice::mix::GvrTextureFormat::RGB5A3 },
        { "RGBA8", spice::mix::GvrTextureFormat::RGBA8 }, { "CI4", spice::mix::GvrTextureFormat::CI4 },
        { "CI8", spice::mix::GvrTextureFormat::CI8 }, { "CI14X2", spice::mix::GvrTextureFormat::CI14X2 },
        { "CMPR", spice::mix::GvrTextureFormat::CMPR },
    };
    for (const auto& [name, value] : formats) controls.format->addItem(name, static_cast<int>(value));
    form->addRow("Format", controls.format);

    controls.palette = new QComboBox(controls.group);
    controls.palette->addItem("Preserve current", -1);
    controls.palette->addItem("IA8", static_cast<int>(spice::mix::GvrPaletteFormat::IA8));
    controls.palette->addItem("RGB565", static_cast<int>(spice::mix::GvrPaletteFormat::RGB565));
    controls.palette->addItem("RGB5A3", static_cast<int>(spice::mix::GvrPaletteFormat::RGB5A3));
    form->addRow("Palette", controls.palette);

    controls.mipmaps = new QComboBox(controls.group);
    controls.mipmaps->addItem("Preserve current", -1);
    controls.mipmaps->addItem("Disabled", 0);
    controls.mipmaps->addItem("Enabled", 1);
    form->addRow("Mipmaps", controls.mipmaps);

    controls.globalMode = new QComboBox(controls.group);
    controls.globalMode->addItem("Preserve current", static_cast<int>(spice::mix::GvrGlobalIndexKind::Preserve));
    controls.globalMode->addItem("None", static_cast<int>(spice::mix::GvrGlobalIndexKind::None));
    controls.globalMode->addItem("Value", static_cast<int>(spice::mix::GvrGlobalIndexKind::Value));
    controls.globalValue = new QDoubleSpinBox(controls.group);
    controls.globalValue->setDecimals(0);
    controls.globalValue->setRange(0, 4294967295.0);
    auto* globalRow = new QWidget(controls.group);
    auto* globalLayout = new QHBoxLayout(globalRow);
    globalLayout->setContentsMargins(0, 0, 0, 0);
    globalLayout->addWidget(controls.globalMode);
    globalLayout->addWidget(controls.globalValue);
    form->addRow("Global index", globalRow);

    if (includeAklz) {
        controls.aklz = new QComboBox(controls.group);
        controls.aklz->addItem("Preserve current", static_cast<int>(spice::mix::AklzPolicy::Preserve));
        controls.aklz->addItem("Raw", static_cast<int>(spice::mix::AklzPolicy::Raw));
        controls.aklz->addItem("AKLZ compressed", static_cast<int>(spice::mix::AklzPolicy::Compressed));
        form->addRow("Wrapper", controls.aklz);
    }
    layout->addWidget(controls.group);
    const auto setExpanded = [group = controls.group](const bool expanded) {
        const auto children = group->findChildren<QWidget*>(QString(), Qt::FindDirectChildrenOnly);
        for (auto* child : children) child->setVisible(expanded);
        group->setMaximumHeight(expanded ? QWIDGETSIZE_MAX : 30);
    };
    QObject::connect(controls.group, &QGroupBox::toggled, controls.group, setExpanded);
    setExpanded(false);
    return controls;
}

QString encodingName(const spice::mix::TextureEncodingKind kind) {
    switch (kind) {
    case spice::mix::TextureEncodingKind::Gvr: return "GVR";
    case spice::mix::TextureEncodingKind::Pvr: return "PVR";
    default: return "Unknown";
    }
}

} // namespace

struct MldWorkbench::Impl {
    MldWorkbench* owner = nullptr;
    std::shared_ptr<spice::mix::MldDocumentSession> session{};
    RackTaskController* tasks = nullptr;
    QTabWidget* pages = nullptr;
    QLabel* overview = nullptr;
    QTableWidget* entryTable = nullptr;
    QTableWidget* textureTable = nullptr;
    TextureViewport* viewport = nullptr;
    QLabel* textureDetails = nullptr;
    QTextEdit* diagnosticText = nullptr;
    QTextEdit* textureDiagnostics = nullptr;
    QLabel* readOnlyExplanation = nullptr;
    QLabel* exportStagedNotice = nullptr;
    QPushButton* replaceButton = nullptr;
    QPushButton* exportBlenderIrButton = nullptr;
    QPushButton* exportEntryListButton = nullptr;
    QPushButton* exportBothButton = nullptr;
    QCheckBox* allowDimensions = nullptr;
    EncodingControls encoding{};

    void refreshOverview() {
        const auto item = session->overview();
        overview->setText(QString(
            "<b>%1</b><br>Platform: %2 &nbsp; Endian: %3 &nbsp; Parse: %4<br>"
            "AKLZ source: %5<br>Entries: %6 &nbsp; Textures: %7 &nbsp; Objects: %8 &nbsp; Ground: %9 &nbsp; Motions: %10")
            .arg(qpath(item.sourcePath), QString::fromStdString(item.platform), QString::fromStdString(item.endian),
                QString::fromStdString(item.parseStatus), item.sourceWasAklz ? "yes" : "no")
            .arg(item.entryCount).arg(item.textureCount).arg(item.objectResourceCount)
            .arg(item.groundResourceCount).arg(item.motionResourceCount));
        if (exportStagedNotice) exportStagedNotice->setVisible(session->dirty());
    }

    void refreshEntries() {
        const auto entries = session->entries();
        entryTable->setRowCount(static_cast<int>(entries.size()));
        for (int row = 0; row < static_cast<int>(entries.size()); ++row) {
            const auto& item = entries[static_cast<std::size_t>(row)];
            const QString values[] = {
                QString::number(item.tableIndex), QString::number(item.entryId), QString::number(item.tableId),
                QString::fromStdString(item.functionName),
                QString("%1, %2, %3").arg(item.positionX).arg(item.positionY).arg(item.positionZ),
                QString("%1, %2, %3").arg(item.rotationX).arg(item.rotationY).arg(item.rotationZ),
                QString("%1, %2, %3").arg(item.scaleX).arg(item.scaleY).arg(item.scaleZ),
                QString::number(item.objectCount), QString::number(item.groundCount), QString::number(item.motionCount),
                QString("0x%1").arg(item.texturesPointer, 0, 16),
            };
            for (int column = 0; column < 11; ++column) entryTable->setItem(row, column, new QTableWidgetItem(values[column]));
        }
    }

    void refreshTextures() {
        const int selected = textureTable->currentRow();
        const auto textures = session->textures();
        textureTable->setRowCount(static_cast<int>(textures.size()));
        for (int row = 0; row < static_cast<int>(textures.size()); ++row) {
            const auto& item = textures[static_cast<std::size_t>(row)];
            const QString values[] = {
                QString::number(item.index), QString::fromStdString(item.name), encodingName(item.encoding),
                QString::fromStdString(item.format), QString("%1 x %2").arg(item.width).arg(item.height),
                item.dirty ? "Staged" : "Original",
            };
            for (int column = 0; column < 6; ++column) textureTable->setItem(row, column, new QTableWidgetItem(values[column]));
        }
        if (!textures.empty()) textureTable->selectRow(std::clamp(selected, 0, static_cast<int>(textures.size()) - 1));
        refreshSelectedTexture();
        refreshOverview();
        owner->notifyStateChanged();
    }

    void refreshSelectedTexture() {
        const int row = textureTable->currentRow();
        const auto textures = session->textures();
        if (row < 0 || row >= static_cast<int>(textures.size())) {
            viewport->setImage(std::nullopt, "No texture selected");
            textureDetails->clear();
            replaceButton->setEnabled(false);
            allowDimensions->setEnabled(false);
            encoding.group->setEnabled(false);
            readOnlyExplanation->hide();
            return;
        }
        const auto& item = textures[static_cast<std::size_t>(row)];
        viewport->setImage(session->texturePreview(static_cast<std::size_t>(row)));
        textureDetails->setText(QString(
            "<b>%1</b><br>%2 | %3<br>%4 x %5<br>Palette: %6<br>Mipmaps: %7<br>Global index: %8<br>Encoded size: %9 bytes")
            .arg(QString::fromStdString(item.name))
            .arg(encodingName(item.encoding), QString::fromStdString(item.format))
            .arg(item.width).arg(item.height).arg(QString::fromStdString(item.paletteFormat))
            .arg(item.mipmaps ? "yes" : "no")
            .arg(item.hasGlobalIndex ? QString::number(item.globalIndex) : "none")
            .arg(item.encodedSize));
        const bool editable = item.encoding == spice::mix::TextureEncodingKind::Gvr;
        replaceButton->setEnabled(editable);
        allowDimensions->setEnabled(editable);
        encoding.group->setEnabled(editable);
        readOnlyExplanation->setVisible(!editable);
    }

    void refreshDiagnostics() {
        QString text{};
        for (const auto& diagnostic : session->diagnostics()) {
            const char* level = diagnostic.level == spice::mix::EventLevel::Error ? "ERROR"
                : diagnostic.level == spice::mix::EventLevel::Warning ? "WARNING" : "INFO";
            text += QString("[%1] %2").arg(level, QString::fromStdString(diagnostic.message));
            if (diagnostic.sourceOffset.has_value()) text += QString(" @0x%1").arg(*diagnostic.sourceOffset, 0, 16);
            text += '\n';
        }
        diagnosticText->setPlainText(text);
        if (textureDiagnostics) textureDiagnostics->setPlainText(text);
    }
};

MldWorkbench::MldWorkbench(std::shared_ptr<spice::mix::MldDocumentSession> session,
    RackTaskController& tasks, QWidget* parent)
    : DocumentWorkbench(parent), impl_(std::make_unique<Impl>()) {
    impl_->owner = this;
    impl_->session = std::move(session);
    impl_->tasks = &tasks;
    auto* root = new QVBoxLayout(this);
    impl_->pages = new QTabWidget(this);
    root->addWidget(impl_->pages);

    auto* overviewPage = new QWidget(impl_->pages);
    auto* overviewLayout = new QVBoxLayout(overviewPage);
    impl_->overview = new QLabel(overviewPage);
    impl_->overview->setTextInteractionFlags(Qt::TextSelectableByMouse);
    overviewLayout->addWidget(impl_->overview);
    overviewLayout->addStretch();
    impl_->pages->addTab(overviewPage, "Overview");

    auto* entryPage = new QWidget(impl_->pages);
    auto* entryLayout = new QVBoxLayout(entryPage);
    impl_->entryTable = new QTableWidget(entryPage);
    impl_->entryTable->setColumnCount(11);
    impl_->entryTable->setHorizontalHeaderLabels({ "Index", "Entry ID", "Table ID", "Function", "Position",
        "Rotation", "Scale", "Objects", "Ground", "Motions", "Textures Ptr" });
    impl_->entryTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    impl_->entryTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    impl_->entryTable->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
    entryLayout->addWidget(impl_->entryTable);
    impl_->pages->addTab(entryPage, "Entries");

    auto* texturePage = new QWidget(impl_->pages);
    auto* textureRoot = new QVBoxLayout(texturePage);
    textureRoot->setContentsMargins(0, 0, 0, 0);
    auto* splitter = new QSplitter(Qt::Horizontal, texturePage);
    impl_->textureTable = new QTableWidget(splitter);
    impl_->textureTable->setMinimumWidth(280);
    impl_->textureTable->setColumnCount(6);
    impl_->textureTable->setHorizontalHeaderLabels({ "Index", "Name", "Kind", "Format", "Size", "State" });
    impl_->textureTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    impl_->textureTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    impl_->textureTable->setSelectionMode(QAbstractItemView::SingleSelection);
    impl_->textureTable->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
    impl_->viewport = new TextureViewport(splitter);
    impl_->viewport->setMinimumSize(320, 260);

    auto* sidebar = new QWidget(splitter);
    sidebar->setObjectName("textureEditorSidebar");
    sidebar->setMinimumWidth(260);
    auto* detailLayout = new QVBoxLayout(sidebar);
    auto* metadataTitle = new QLabel("<b>Texture metadata</b>", sidebar);
    impl_->textureDetails = new QLabel(sidebar);
    impl_->textureDetails->setWordWrap(true);
    impl_->textureDetails->setTextInteractionFlags(Qt::TextSelectableByMouse);
    detailLayout->addWidget(metadataTitle);
    detailLayout->addWidget(impl_->textureDetails);
    impl_->replaceButton = new QPushButton("Replace from PNG...", sidebar);
    auto* exportPng = new QPushButton("Export PNG...", sidebar);
    auto* extract = new QPushButton("Extract native...", sidebar);
    auto* revert = new QPushButton("Revert texture", sidebar);
    auto* revertAll = new QPushButton("Revert all staged texture changes", sidebar);
    detailLayout->addWidget(impl_->replaceButton);
    detailLayout->addWidget(exportPng);
    detailLayout->addWidget(extract);
    detailLayout->addWidget(revert);
    detailLayout->addWidget(revertAll);
    impl_->readOnlyExplanation = new QLabel(
        "PVR texture: preview, PNG export, and native extraction are available. Replacement and encoding are read-only.",
        sidebar);
    impl_->readOnlyExplanation->setWordWrap(true);
    impl_->readOnlyExplanation->setStyleSheet("color: palette(mid);");
    detailLayout->addWidget(impl_->readOnlyExplanation);
    impl_->allowDimensions = new QCheckBox("Allow replacement dimension changes", sidebar);
    detailLayout->addWidget(impl_->allowDimensions);
    impl_->encoding = addEncodingControls(detailLayout, false);
    detailLayout->addStretch();
    detailLayout->addWidget(new QLabel("<b>Diagnostics</b>", sidebar));
    impl_->textureDiagnostics = new QTextEdit(sidebar);
    impl_->textureDiagnostics->setReadOnly(true);
    impl_->textureDiagnostics->setMaximumHeight(120);
    detailLayout->addWidget(impl_->textureDiagnostics);
    splitter->addWidget(impl_->textureTable);
    splitter->addWidget(impl_->viewport);
    splitter->addWidget(sidebar);
    splitter->setStretchFactor(1, 1);
    splitter->setCollapsible(0, false);
    splitter->setCollapsible(1, false);
    splitter->setCollapsible(2, false);
    splitter->setSizes({ 340, 620, 320 });
    textureRoot->addWidget(splitter);
    impl_->pages->addTab(texturePage, "Textures");

    auto* exportsPage = new QWidget(impl_->pages);
    exportsPage->setObjectName("mldExportsPage");
    auto* exportsLayout = new QVBoxLayout(exportsPage);
    impl_->exportStagedNotice = new QLabel(
        "This document has staged changes. Exports will represent the current workbench state.", exportsPage);
    impl_->exportStagedNotice->setObjectName("mldExportStagedNotice");
    impl_->exportStagedNotice->setWordWrap(true);
    impl_->exportStagedNotice->setStyleSheet(
        "QLabel { background: #5c4818; color: #ffe29a; border: 1px solid #9a7730; padding: 6px; }");
    exportsLayout->addWidget(impl_->exportStagedNotice);

    auto* blenderGroup = new QGroupBox("Blender IR JSON", exportsPage);
    auto* blenderLayout = new QVBoxLayout(blenderGroup);
    auto* blenderDescription = new QLabel(
        "Export the current MLD scene, geometry, materials, textures, instances, and animations as Blender intermediate representation JSON.",
        blenderGroup);
    blenderDescription->setWordWrap(true);
    impl_->exportBlenderIrButton = new QPushButton("Export Blender IR...", blenderGroup);
    impl_->exportBlenderIrButton->setObjectName("mldExportBlenderIrButton");
    blenderLayout->addWidget(blenderDescription);
    blenderLayout->addWidget(impl_->exportBlenderIrButton, 0, Qt::AlignLeft);
    exportsLayout->addWidget(blenderGroup);

    auto* entryGroup = new QGroupBox("Detailed Entry JSON", exportsPage);
    auto* exportEntryLayout = new QVBoxLayout(entryGroup);
    auto* entryDescription = new QLabel(
        "Export spice_mld_entry_list_v1 with entry IDs, functions, counts, pointers, links, parameters, resource addresses, and texture names.",
        entryGroup);
    entryDescription->setWordWrap(true);
    impl_->exportEntryListButton = new QPushButton("Export Entry JSON...", entryGroup);
    impl_->exportEntryListButton->setObjectName("mldExportEntryListButton");
    exportEntryLayout->addWidget(entryDescription);
    exportEntryLayout->addWidget(impl_->exportEntryListButton, 0, Qt::AlignLeft);
    exportsLayout->addWidget(entryGroup);

    impl_->exportBothButton = new QPushButton("Export Both to Folder...", exportsPage);
    impl_->exportBothButton->setObjectName("mldExportBothButton");
    exportsLayout->addWidget(impl_->exportBothButton, 0, Qt::AlignLeft);
    exportsLayout->addStretch();
    impl_->pages->addTab(exportsPage, "Exports");

    auto* diagnosticsPage = new QWidget(impl_->pages);
    auto* diagnosticsLayout = new QVBoxLayout(diagnosticsPage);
    impl_->diagnosticText = new QTextEdit(diagnosticsPage);
    impl_->diagnosticText->setReadOnly(true);
    diagnosticsLayout->addWidget(impl_->diagnosticText);
    impl_->pages->addTab(diagnosticsPage, "Diagnostics");

    connect(impl_->textureTable, &QTableWidget::currentCellChanged, this,
        [this](int, int, int, int) { impl_->refreshSelectedTexture(); });
    connect(impl_->exportBlenderIrButton, &QPushButton::clicked, this, [this]() {
        const auto source = impl_->session->overview().sourcePath;
        const auto suggested = source.parent_path() / mldBlenderIrFilename(source);
        const auto output = QFileDialog::getSaveFileName(this, "Export MLD Blender IR JSON",
            qpath(suggested), "JSON files (*.json)");
        if (output.isEmpty()) return;
        auto result = std::make_shared<spice::mix::DocumentResult>();
        QPointer<MldWorkbench> self(this);
        if (!impl_->tasks->run("Export MLD Blender IR", [session = impl_->session,
            path = fspath(output), result](const auto& context) {
                *result = session->exportBlenderIrJson(path, context);
            }, [self, result]() {
                if (self) showResult(self, *result);
            })) {
            QMessageBox::information(this, "SpiceRack", "Finish or cancel the current job first.");
        }
    });
    connect(impl_->exportEntryListButton, &QPushButton::clicked, this, [this]() {
        const auto source = impl_->session->overview().sourcePath;
        const auto suggested = source.parent_path() / mldEntryListFilename(source);
        const auto output = QFileDialog::getSaveFileName(this, "Export Detailed MLD Entry JSON",
            qpath(suggested), "JSON files (*.json)");
        if (output.isEmpty()) return;
        auto result = std::make_shared<spice::mix::DocumentResult>();
        QPointer<MldWorkbench> self(this);
        if (!impl_->tasks->run("Export MLD entry JSON", [session = impl_->session,
            path = fspath(output), result](const auto& context) {
                *result = session->exportEntryListJson(path, context);
            }, [self, result]() {
                if (self) showResult(self, *result);
            })) {
            QMessageBox::information(this, "SpiceRack", "Finish or cancel the current job first.");
        }
    });
    connect(impl_->exportBothButton, &QPushButton::clicked, this, [this]() {
        const auto source = impl_->session->overview().sourcePath;
        const auto selectedFolder = QFileDialog::getExistingDirectory(
            this, "Export MLD JSON Artifacts", qpath(source.parent_path()));
        if (selectedFolder.isEmpty()) return;
        const auto folder = fspath(selectedFolder);
        const auto blenderPath = folder / mldBlenderIrFilename(source);
        const auto entryPath = folder / mldEntryListFilename(source);
        std::error_code error{};
        const bool blenderExists = std::filesystem::exists(blenderPath, error) && !error;
        error.clear();
        const bool entryExists = std::filesystem::exists(entryPath, error) && !error;
        if ((blenderExists || entryExists)
            && QMessageBox::question(this, "Replace existing exports",
                "One or more generated export files already exist in that folder. Replace them?",
                QMessageBox::Yes | QMessageBox::No, QMessageBox::No) != QMessageBox::Yes) {
            return;
        }

        auto result = std::make_shared<spice::mix::DocumentResult>();
        QPointer<MldWorkbench> self(this);
        if (!impl_->tasks->run("Export MLD JSON artifacts", [session = impl_->session,
            blenderPath, entryPath, result](const auto& context) {
                const auto blenderResult = session->exportBlenderIrJson(blenderPath, context);
                if (!blenderResult.ok()) {
                    *result = blenderResult;
                    return;
                }
                const auto entryResult = session->exportEntryListJson(entryPath, context);
                if (!entryResult.ok()) {
                    *result = entryResult;
                    result->message = "Blender IR was exported to " + blenderPath.string()
                        + ", but the entry-list export did not complete: " + entryResult.message;
                    if (context.report) {
                        context.report({ .level = spice::mix::EventLevel::Warning,
                            .message = result->message });
                    }
                    return;
                }
                result->message = "Exported MLD Blender IR and detailed entry-list JSON.";
            }, [self, result]() {
                if (self) showResult(self, *result);
            })) {
            QMessageBox::information(this, "SpiceRack", "Finish or cancel the current job first.");
        }
    });
    connect(impl_->replaceButton, &QPushButton::clicked, this, [this]() {
        const int row = impl_->textureTable->currentRow();
        if (row < 0) return;
        const auto input = QFileDialog::getOpenFileName(this, "Replacement PNG", {}, "PNG images (*.png)");
        if (input.isEmpty()) return;
        const auto result = std::make_shared<spice::mix::DocumentResult>();
        QPointer<MldWorkbench> self(this);
        impl_->tasks->run("Replace MLD texture", [session = impl_->session, row, inputPath = fspath(input),
            settings = impl_->encoding.overrides(), allow = impl_->allowDimensions->isChecked(), result](const auto& context) {
                *result = session->replaceGvrTexture(static_cast<std::size_t>(row), inputPath, settings, allow, context);
            }, [self, result]() {
                if (!self) return;
                showResult(self, *result);
                if (result->ok()) self->impl_->refreshTextures();
            });
    });
    connect(extract, &QPushButton::clicked, this, [this]() {
        const int row = impl_->textureTable->currentRow();
        const auto textures = impl_->session->textures();
        if (row < 0 || row >= static_cast<int>(textures.size())) return;
        const auto extension = textures[static_cast<std::size_t>(row)].encoding == spice::mix::TextureEncodingKind::Pvr ? "pvr" : "gvr";
        const auto output = QFileDialog::getSaveFileName(this, "Extract native texture", {},
            QString("Native texture (*.%1)").arg(extension));
        if (output.isEmpty()) return;
        const auto result = std::make_shared<spice::mix::DocumentResult>();
        QPointer<MldWorkbench> self(this);
        impl_->tasks->run("Extract native texture", [session = impl_->session, row, path = fspath(output), result](const auto& context) {
            *result = session->extractNativeTexture(static_cast<std::size_t>(row), path, context);
        }, [self, result]() { if (self) showResult(self, *result); });
    });
    connect(exportPng, &QPushButton::clicked, this, [this]() {
        const int row = impl_->textureTable->currentRow();
        if (row < 0) return;
        const auto output = QFileDialog::getSaveFileName(this, "Export texture PNG", {}, "PNG images (*.png)");
        if (output.isEmpty()) return;
        const auto result = std::make_shared<spice::mix::DocumentResult>();
        QPointer<MldWorkbench> self(this);
        impl_->tasks->run("Export texture PNG", [session = impl_->session, row, path = fspath(output), result](const auto& context) {
            *result = session->exportTexturePng(static_cast<std::size_t>(row), path, context);
        }, [self, result]() { if (self) showResult(self, *result); });
    });
    connect(revert, &QPushButton::clicked, this, [this]() {
        const int row = impl_->textureTable->currentRow();
        if (row < 0) return;
        const auto result = impl_->session->revertTexture(static_cast<std::size_t>(row));
        showResult(this, result);
        if (result.ok()) impl_->refreshTextures();
    });
    connect(revertAll, &QPushButton::clicked, this, [this]() {
        const auto result = impl_->session->revertAll();
        showResult(this, result);
        if (result.ok()) impl_->refreshTextures();
    });

    impl_->refreshOverview();
    impl_->refreshEntries();
    impl_->refreshTextures();
    impl_->refreshDiagnostics();
}

MldWorkbench::~MldWorkbench() = default;
QString MldWorkbench::displayName() const { return qpath(impl_->session->overview().sourcePath.filename()); }
bool MldWorkbench::dirty() const { return impl_->session->dirty(); }
std::optional<std::filesystem::path> MldWorkbench::sourcePath() const { return impl_->session->overview().sourcePath; }
bool MldWorkbench::runSmokeChecks() {
    const int exportsIndex = impl_->pages->indexOf(impl_->exportStagedNotice->parentWidget());
    const bool exportsPageReady = exportsIndex >= 0
        && impl_->pages->tabText(exportsIndex) == "Exports"
        && exportsIndex + 1 < impl_->pages->count()
        && impl_->pages->tabText(exportsIndex + 1) == "Diagnostics"
        && impl_->exportBlenderIrButton
        && impl_->exportEntryListButton
        && impl_->exportBothButton
        && impl_->exportStagedNotice->isHidden() == !dirty();
    return impl_->viewport
        && exportsPageReady
        && impl_->viewport->samplingMode() == TextureViewport::SamplingMode::Nearest
        && impl_->viewport->zoomMode() == TextureViewport::ZoomMode::IntegerFit
        && impl_->viewport->verifyViewControlsDoNotInvoke([this]() { return dirty(); });
}

void MldWorkbench::requestSaveAs(std::function<void(bool)> completed) {
    if (impl_->tasks->busy()) {
        QMessageBox::information(this, "SpiceRack", "Finish or cancel the current job first.");
        if (completed) completed(false);
        return;
    }
    const auto output = QFileDialog::getSaveFileName(this, "Save MLD As", {}, "MLD files (*.mld)");
    if (output.isEmpty()) { if (completed) completed(false); return; }
    auto result = std::make_shared<spice::mix::DocumentResult>();
    QPointer<MldWorkbench> self(this);
    impl_->tasks->run("Save MLD As", [session = impl_->session, path = fspath(output), result](const auto& context) {
        *result = session->saveAs(path, context);
    }, [self, result, completed = std::move(completed)]() mutable {
        if (!self) return;
        showResult(self, *result);
        if (result->ok()) self->impl_->refreshTextures();
        if (completed) completed(result->ok());
    });
}

struct GvrWorkbench::Impl {
    GvrWorkbench* owner = nullptr;
    std::shared_ptr<spice::mix::GvrDocumentSession> session{};
    RackTaskController* tasks = nullptr;
    TextureViewport* viewport = nullptr;
    QLabel* details = nullptr;
    QTextEdit* diagnostics = nullptr;
    QCheckBox* allowDimensions = nullptr;
    EncodingControls encoding{};

    void refresh() {
        const auto item = session->snapshot();
        viewport->setImage(session->preview());
        details->setText(QString("<b>%1</b><br>%2 x %3<br>Palette: %4<br>Mipmaps: %5<br>Global index: %6<br>Wrapper: %7")
            .arg(QString::fromStdString(item.format)).arg(item.width).arg(item.height)
            .arg(QString::fromStdString(item.paletteFormat)).arg(item.mipmaps ? "yes" : "no")
            .arg(item.hasGlobalIndex ? QString::number(item.globalIndex) : "none")
            .arg(item.aklzWrapped ? "AKLZ" : "raw"));
        QString diagnosticText{};
        for (const auto& diagnostic : item.diagnostics) diagnosticText += QString::fromStdString(diagnostic) + '\n';
        diagnostics->setPlainText(diagnosticText);
        owner->notifyStateChanged();
    }
};

GvrWorkbench::GvrWorkbench(std::shared_ptr<spice::mix::GvrDocumentSession> session,
    RackTaskController& tasks, QWidget* parent)
    : DocumentWorkbench(parent), impl_(std::make_unique<Impl>()) {
    impl_->owner = this;
    impl_->session = std::move(session);
    impl_->tasks = &tasks;
    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    auto* splitter = new QSplitter(Qt::Horizontal, this);
    impl_->viewport = new TextureViewport(splitter);
    impl_->viewport->setMinimumSize(480, 300);
    auto* sidebar = new QWidget(splitter);
    sidebar->setObjectName("textureEditorSidebar");
    sidebar->setMinimumWidth(260);
    auto* sidebarLayout = new QVBoxLayout(sidebar);
    sidebarLayout->addWidget(new QLabel("<b>Texture metadata</b>", sidebar));
    impl_->details = new QLabel(sidebar);
    impl_->details->setWordWrap(true);
    impl_->details->setTextInteractionFlags(Qt::TextSelectableByMouse);
    sidebarLayout->addWidget(impl_->details);
    auto* replace = new QPushButton("Replace from PNG...", sidebar);
    auto* exportPng = new QPushButton("Export PNG...", sidebar);
    auto* revert = new QPushButton("Revert", sidebar);
    sidebarLayout->addWidget(replace);
    sidebarLayout->addWidget(exportPng);
    sidebarLayout->addWidget(revert);
    impl_->allowDimensions = new QCheckBox("Allow replacement dimension changes", sidebar);
    impl_->allowDimensions->setChecked(true);
    sidebarLayout->addWidget(impl_->allowDimensions);
    impl_->encoding = addEncodingControls(sidebarLayout, true);
    sidebarLayout->addStretch();
    sidebarLayout->addWidget(new QLabel("<b>Diagnostics</b>", sidebar));
    impl_->diagnostics = new QTextEdit(sidebar);
    impl_->diagnostics->setReadOnly(true);
    impl_->diagnostics->setMaximumHeight(140);
    sidebarLayout->addWidget(impl_->diagnostics);
    splitter->addWidget(impl_->viewport);
    splitter->addWidget(sidebar);
    splitter->setStretchFactor(0, 1);
    splitter->setCollapsible(0, false);
    splitter->setCollapsible(1, false);
    splitter->setSizes({ 900, 320 });
    root->addWidget(splitter);

    connect(replace, &QPushButton::clicked, this, [this]() {
        const auto input = QFileDialog::getOpenFileName(this, "Replacement PNG", {}, "PNG images (*.png)");
        if (input.isEmpty()) return;
        auto result = std::make_shared<spice::mix::DocumentResult>();
        spice::mix::GvrSaveOptions options{};
        options.encoding = impl_->encoding.overrides();
        options.aklz = impl_->encoding.aklzPolicy();
        QPointer<GvrWorkbench> self(this);
        impl_->tasks->run("Replace GVR image", [session = impl_->session, path = fspath(input), options,
            allow = impl_->allowDimensions->isChecked(), result](const auto& context) {
            *result = session->replaceImage(path, options, allow, context);
        }, [self, result]() {
            if (!self) return;
            showResult(self, *result);
            if (result->ok()) self->impl_->refresh();
        });
    });
    connect(exportPng, &QPushButton::clicked, this, [this]() {
        const auto output = QFileDialog::getSaveFileName(this, "Export PNG", {}, "PNG images (*.png)");
        if (output.isEmpty()) return;
        auto result = std::make_shared<spice::mix::DocumentResult>();
        QPointer<GvrWorkbench> self(this);
        impl_->tasks->run("Export GVR PNG", [session = impl_->session, path = fspath(output), result](const auto& context) {
            *result = session->exportPng(path, context);
        }, [self, result]() { if (self) showResult(self, *result); });
    });
    connect(revert, &QPushButton::clicked, this, [this]() {
        const auto result = impl_->session->revert();
        showResult(this, result);
        if (result.ok()) impl_->refresh();
    });
    impl_->refresh();
}

GvrWorkbench::~GvrWorkbench() = default;
QString GvrWorkbench::displayName() const { return QString::fromStdString(impl_->session->snapshot().displayName); }
bool GvrWorkbench::dirty() const { return impl_->session->dirty(); }
std::optional<std::filesystem::path> GvrWorkbench::sourcePath() const { return impl_->session->snapshot().sourcePath; }
bool GvrWorkbench::runSmokeChecks() {
    return impl_->viewport
        && impl_->viewport->samplingMode() == TextureViewport::SamplingMode::Nearest
        && impl_->viewport->zoomMode() == TextureViewport::ZoomMode::IntegerFit
        && impl_->viewport->verifyViewControlsDoNotInvoke([this]() { return dirty(); });
}

void GvrWorkbench::requestSaveAs(std::function<void(bool)> completed) {
    if (impl_->tasks->busy()) {
        QMessageBox::information(this, "SpiceRack", "Finish or cancel the current job first.");
        if (completed) completed(false);
        return;
    }
    const auto output = QFileDialog::getSaveFileName(this, "Save GVR As", {}, "GVR files (*.gvr)");
    if (output.isEmpty()) { if (completed) completed(false); return; }
    auto result = std::make_shared<spice::mix::DocumentResult>();
    QPointer<GvrWorkbench> self(this);
    impl_->tasks->run("Save GVR As", [session = impl_->session, path = fspath(output), result](const auto& context) {
        *result = session->saveAs(path, context);
    }, [self, result, completed = std::move(completed)]() mutable {
        if (!self) return;
        showResult(self, *result);
        if (result->ok()) self->impl_->refresh();
        if (completed) completed(result->ok());
    });
}
