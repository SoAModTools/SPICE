#include "DocumentWorkbenches.h"

#include <QtCore/QPointer>
#include <QtGui/QImage>
#include <QtGui/QPixmap>
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

QPixmap previewPixmap(const std::optional<spice::mix::RgbaImageSnapshot>& preview,
    const QSize& target = QSize(520, 520)) {
    if (!preview.has_value() || preview->empty()) return {};
    QImage image(preview->rgba8.data(), static_cast<int>(preview->width),
        static_cast<int>(preview->height), QImage::Format_RGBA8888);
    return QPixmap::fromImage(image.copy()).scaled(target, Qt::KeepAspectRatio, Qt::SmoothTransformation);
}

void showResult(QWidget* parent, const spice::mix::DocumentResult& result) {
    if (result.status == spice::mix::OperationStatus::Failure) {
        QMessageBox::critical(parent, "SpiceRack", QString::fromStdString(result.message));
    } else if (result.status == spice::mix::OperationStatus::Cancelled) {
        QMessageBox::information(parent, "SpiceRack", "The operation was cancelled.");
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
    QLabel* preview = nullptr;
    QLabel* textureDetails = nullptr;
    QTextEdit* diagnosticText = nullptr;
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
            preview->setPixmap({});
            preview->setText("No texture selected");
            textureDetails->clear();
            return;
        }
        const auto& item = textures[static_cast<std::size_t>(row)];
        const auto pixmap = previewPixmap(session->texturePreview(static_cast<std::size_t>(row)));
        preview->setPixmap(pixmap);
        if (pixmap.isNull()) preview->setText("No decoded preview available");
        textureDetails->setText(QString(
            "%1 | %2 | %3 x %4 | palette %5 | mipmaps %6 | global index %7 | %8 bytes")
            .arg(encodingName(item.encoding), QString::fromStdString(item.format))
            .arg(item.width).arg(item.height).arg(QString::fromStdString(item.paletteFormat))
            .arg(item.mipmaps ? "yes" : "no")
            .arg(item.hasGlobalIndex ? QString::number(item.globalIndex) : "none")
            .arg(item.encodedSize));
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
    auto* splitter = new QSplitter(texturePage);
    impl_->textureTable = new QTableWidget(splitter);
    impl_->textureTable->setColumnCount(6);
    impl_->textureTable->setHorizontalHeaderLabels({ "Index", "Name", "Kind", "Format", "Size", "State" });
    impl_->textureTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    impl_->textureTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    impl_->textureTable->setSelectionMode(QAbstractItemView::SingleSelection);
    impl_->textureTable->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
    auto* detail = new QWidget(splitter);
    auto* detailLayout = new QVBoxLayout(detail);
    impl_->preview = new QLabel("No texture selected", detail);
    impl_->preview->setAlignment(Qt::AlignCenter);
    impl_->preview->setMinimumSize(320, 260);
    impl_->textureDetails = new QLabel(detail);
    impl_->textureDetails->setWordWrap(true);
    detailLayout->addWidget(impl_->preview, 1);
    detailLayout->addWidget(impl_->textureDetails);
    auto* controlsRow = new QHBoxLayout();
    auto* replace = new QPushButton("Replace from PNG...", detail);
    auto* extract = new QPushButton("Extract native...", detail);
    auto* exportPng = new QPushButton("Export PNG...", detail);
    auto* revert = new QPushButton("Revert texture", detail);
    controlsRow->addWidget(replace);
    controlsRow->addWidget(extract);
    controlsRow->addWidget(exportPng);
    controlsRow->addWidget(revert);
    detailLayout->addLayout(controlsRow);
    impl_->allowDimensions = new QCheckBox("Allow replacement dimension changes", detail);
    detailLayout->addWidget(impl_->allowDimensions);
    impl_->encoding = addEncodingControls(detailLayout, false);
    splitter->addWidget(impl_->textureTable);
    splitter->addWidget(detail);
    splitter->setStretchFactor(1, 1);
    textureRoot->addWidget(splitter);
    auto* revertAll = new QPushButton("Revert all staged texture changes", texturePage);
    textureRoot->addWidget(revertAll, 0, Qt::AlignRight);
    impl_->pages->addTab(texturePage, "Textures");

    auto* diagnosticsPage = new QWidget(impl_->pages);
    auto* diagnosticsLayout = new QVBoxLayout(diagnosticsPage);
    impl_->diagnosticText = new QTextEdit(diagnosticsPage);
    impl_->diagnosticText->setReadOnly(true);
    diagnosticsLayout->addWidget(impl_->diagnosticText);
    impl_->pages->addTab(diagnosticsPage, "Diagnostics");

    connect(impl_->textureTable, &QTableWidget::currentCellChanged, this,
        [this](int, int, int, int) { impl_->refreshSelectedTexture(); });
    connect(replace, &QPushButton::clicked, this, [this]() {
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
    QLabel* preview = nullptr;
    QLabel* details = nullptr;
    QTextEdit* diagnostics = nullptr;
    QCheckBox* allowDimensions = nullptr;
    EncodingControls encoding{};

    void refresh() {
        const auto item = session->snapshot();
        const auto pixmap = previewPixmap(session->preview(), QSize(640, 520));
        preview->setPixmap(pixmap);
        if (pixmap.isNull()) preview->setText("No decoded preview available");
        details->setText(QString("%1 | %2 x %3 | palette %4 | mipmaps %5 | global index %6 | wrapper %7")
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
    impl_->preview = new QLabel("No decoded preview available", this);
    impl_->preview->setAlignment(Qt::AlignCenter);
    impl_->preview->setMinimumSize(480, 300);
    root->addWidget(impl_->preview, 1);
    impl_->details = new QLabel(this);
    impl_->details->setWordWrap(true);
    root->addWidget(impl_->details);
    auto* buttons = new QHBoxLayout();
    auto* replace = new QPushButton("Replace from PNG...", this);
    auto* exportPng = new QPushButton("Export PNG...", this);
    auto* revert = new QPushButton("Revert", this);
    buttons->addWidget(replace);
    buttons->addWidget(exportPng);
    buttons->addWidget(revert);
    buttons->addStretch();
    root->addLayout(buttons);
    impl_->allowDimensions = new QCheckBox("Allow replacement dimension changes", this);
    impl_->allowDimensions->setChecked(true);
    root->addWidget(impl_->allowDimensions);
    impl_->encoding = addEncodingControls(root, true);
    impl_->diagnostics = new QTextEdit(this);
    impl_->diagnostics->setReadOnly(true);
    impl_->diagnostics->setMaximumHeight(110);
    root->addWidget(impl_->diagnostics);

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
