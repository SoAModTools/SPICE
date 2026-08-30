#include "DocumentWorkbenches.h"

#include "MldEntryInspector.h"
#include "TextureViewport.h"

#include <QtCore/QStringList>
#include <QtWidgets/QAbstractItemView>
#include <QtWidgets/QHeaderView>
#include <QtWidgets/QLabel>
#include <QtWidgets/QSplitter>
#include <QtWidgets/QTabWidget>
#include <QtWidgets/QTableWidget>
#include <QtWidgets/QTextEdit>
#include <QtWidgets/QVBoxLayout>

#include <algorithm>
#include <iomanip>
#include <sstream>

namespace {

QString qpath(const std::filesystem::path& path) {
    return QString::fromStdWString(path.wstring());
}

QString hexValue(const std::uint64_t value, const int width = 8) {
    return QString("0x%1").arg(value, width, 16, QChar('0')).toUpper();
}

QString vectorText(const std::array<float, 3>& value) {
    return QString("(%1, %2, %3)").arg(value[0], 0, 'g', 7).arg(value[1], 0, 'g', 7).arg(value[2], 0, 'g', 7);
}

QString evidenceText(const spice::mix::SstSmlFieldEvidence evidence) {
    using Evidence = spice::mix::SstSmlFieldEvidence;
    switch (evidence) {
    case Evidence::Gekko: return "Gekko";
    case Evidence::GekkoAndCorpus: return "Gekko + corpus";
    case Evidence::CorpusStable: return "Corpus stable";
    case Evidence::CodeSupportedCorpusAbsent: return "Code-supported / unobserved";
    case Evidence::Provisional: return "Provisional";
    }
    return "Provisional";
}

QString textureKind(const spice::mix::TextureEncodingKind kind) {
    switch (kind) {
    case spice::mix::TextureEncodingKind::Gvr: return "GVR";
    case spice::mix::TextureEncodingKind::Pvr: return "PVR";
    default: return "Unknown";
    }
}

QString levelText(const spice::mix::EventLevel level) {
    switch (level) {
    case spice::mix::EventLevel::Error: return "Error";
    case spice::mix::EventLevel::Warning: return "Warning";
    case spice::mix::EventLevel::Progress: return "Progress";
    default: return "Info";
    }
}

QTableWidgetItem* item(const QString& text) {
    auto* result = new QTableWidgetItem(text);
    result->setFlags(result->flags() & ~Qt::ItemIsEditable);
    return result;
}

void configureTable(QTableWidget* table, const QStringList& headers) {
    table->setColumnCount(headers.size());
    table->setHorizontalHeaderLabels(headers);
    table->setSelectionBehavior(QAbstractItemView::SelectRows);
    table->setSelectionMode(QAbstractItemView::SingleSelection);
    table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table->setAlternatingRowColors(true);
    table->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
    table->horizontalHeader()->setStretchLastSection(true);
}

} // namespace

struct SstSmlWorkbench::Impl {
    std::shared_ptr<spice::mix::SstSmlDocumentSession> session{};
    QTabWidget* pages = nullptr;
    QLabel* overview = nullptr;
    QTableWidget* records = nullptr;
    QTabWidget* embeddedPages = nullptr;
    QLabel* embeddedOverview = nullptr;
    MldEntryInspector* embeddedEntryInspector = nullptr;
    QTableWidget* embeddedTextures = nullptr;
    QLabel* embeddedTextureMetadata = nullptr;
    TextureViewport* embeddedTextureViewport = nullptr;
    QTextEdit* embeddedDiagnostics = nullptr;
    QTableWidget* commands = nullptr;
    QLabel* commandSummary = nullptr;
    QTableWidget* commandFields = nullptr;
    QTableWidget* lightingRows = nullptr;
    QTextEdit* consumerWindows = nullptr;
    QTextEdit* rawPayload = nullptr;
    QLabel* gridSummary = nullptr;
    QTableWidget* grid = nullptr;
    QTextEdit* gridPadding = nullptr;
    QLabel* runtimeSummary = nullptr;
    QTableWidget* runtimeFields = nullptr;
    QTableWidget* slotLinks = nullptr;
    QTableWidget* diagnostics = nullptr;

    void populateOverview() {
        const auto value = session->overview();
        QStringList histogram{};
        for (const auto& [type, count] : session->commandTypeHistogram()) {
            histogram.push_back(QString("Type %1: %2").arg(type).arg(count));
        }
        overview->setText(QString(
            "<h3>%1</h3><p><b>Read-only paired battle-stage inspection</b></p>"
            "<p><b>SML:</b> %2<br><b>SST:</b> %3</p>"
            "<p>SML source/decoded: %4 / %5 bytes (%6)<br>"
            "SST source/decoded: %7 / %8 bytes (%9)</p>"
            "<p><b>Platform context:</b> %10<br>SML byte order: %11 &nbsp; SST byte order: %12</p>"
            "<p>Paired records: %13 &nbsp; Count agreement: %14<br>"
            "Embedded MLD parsed: %15 &nbsp; Failed: %16</p>"
            "<p><b>Command histogram:</b> %17</p>")
            .arg(QString::fromStdString(value.stem).toHtmlEscaped(),
                qpath(value.smlPath).toHtmlEscaped(), qpath(value.sstPath).toHtmlEscaped())
            .arg(value.smlSourceSize).arg(value.smlDecodedSize).arg(value.smlWasAklz ? "AKLZ" : "Raw")
            .arg(value.sstSourceSize).arg(value.sstDecodedSize).arg(value.sstWasAklz ? "AKLZ" : "Raw")
            .arg(QString::fromStdString(value.platformContext), QString::fromStdString(value.smlEndian),
                QString::fromStdString(value.sstEndian))
            .arg(value.recordCount).arg(value.recordCountsAgree ? "Yes" : "No")
            .arg(value.embeddedMldParsedCount).arg(value.embeddedMldFailedCount)
            .arg(histogram.isEmpty() ? "None" : histogram.join("; ")));
    }

    void populateRecords() {
        const auto values = session->records();
        records->setRowCount(static_cast<int>(values.size()));
        for (int row = 0; row < records->rowCount(); ++row) {
            const auto& value = values[static_cast<std::size_t>(row)];
            records->setItem(row, 0, item(QString::number(value.index)));
            records->setItem(row, 1, item(QString::number(value.embeddedMldSize)));
            records->setItem(row, 2, item(QString::fromStdString(value.embeddedMldParseStatus)));
            records->setItem(row, 3, item(QString::number(value.embeddedMldEntryCount)));
            records->setItem(row, 4, item(QString::number(value.embeddedMldTextureCount)));
            records->setItem(row, 5, item(QString::number(value.commandCount)));
            records->setItem(row, 6, item(value.commandBlockValid ? "Valid" : "Invalid"));
            records->setItem(row, 7, item(hexValue(value.embeddedMldOffset)));
            records->setItem(row, 8, item(hexValue(value.commandBlockOffset)));
        }
        if (records->rowCount() > 0) records->setCurrentCell(0, 0);
    }

    void selectRecord(const int row) {
        const auto values = session->records();
        if (row < 0 || static_cast<std::size_t>(row) >= values.size()) {
            embeddedOverview->setText("No paired record selected.");
            embeddedEntryInspector->setEntries({});
            embeddedTextures->setRowCount(0);
            embeddedTextureViewport->setImage(std::nullopt, "No embedded MLD selected");
            commands->setRowCount(0);
            return;
        }
        const auto index = static_cast<std::size_t>(row);
        const auto& record = values[index];
        const auto mld = session->embeddedMldOverview(index);
        if (mld.has_value()) {
            embeddedOverview->setText(QString(
                "<b>Embedded MLD record %1</b><br>Parse: %2<br>Platform: %3 &nbsp; Endian: %4<br>"
                "Entries: %5 &nbsp; Textures: %6 &nbsp; Objects: %7 &nbsp; Ground: %8 &nbsp; Motions: %9<br>"
                "SML payload: %10 + %11 bytes")
                .arg(index).arg(QString::fromStdString(mld->parseStatus), QString::fromStdString(mld->platform),
                    QString::fromStdString(mld->endian))
                .arg(mld->entryCount).arg(mld->textureCount).arg(mld->objectResourceCount)
                .arg(mld->groundResourceCount).arg(mld->motionResourceCount)
                .arg(hexValue(record.embeddedMldOffset)).arg(record.embeddedMldSize));
        } else {
            embeddedOverview->setText(QString("Embedded MLD record %1 is unavailable. SML payload in bounds: %2.")
                .arg(index).arg(record.embeddedMldInBounds ? "Yes" : "No"));
        }

        embeddedEntryInspector->setEntries(session->embeddedMldEntryDetails(index));

        const auto textures = session->embeddedMldTextures(index);
        embeddedTextures->setRowCount(static_cast<int>(textures.size()));
        for (int textureRow = 0; textureRow < embeddedTextures->rowCount(); ++textureRow) {
            const auto& texture = textures[static_cast<std::size_t>(textureRow)];
            embeddedTextures->setItem(textureRow, 0, item(QString::number(texture.index)));
            embeddedTextures->setItem(textureRow, 1, item(QString::fromStdString(texture.name)));
            embeddedTextures->setItem(textureRow, 2, item(textureKind(texture.encoding)));
            embeddedTextures->setItem(textureRow, 3, item(QString::fromStdString(texture.format)));
            embeddedTextures->setItem(textureRow, 4, item(QString("%1 x %2").arg(texture.width).arg(texture.height)));
            embeddedTextures->setItem(textureRow, 5, item(texture.decoded ? "Decoded" : "Unavailable"));
        }
        if (embeddedTextures->rowCount() > 0) embeddedTextures->setCurrentCell(0, 0);
        else {
            embeddedTextureMetadata->setText("No embedded textures.");
            embeddedTextureViewport->setImage(std::nullopt);
        }

        QStringList diagnosticText{};
        for (const auto& diagnostic : session->embeddedMldDiagnostics(index)) {
            diagnosticText.push_back(QString("%1%2%3")
                .arg(levelText(diagnostic.level), diagnostic.sourceOffset.has_value()
                    ? QString(" @ %1: ").arg(hexValue(*diagnostic.sourceOffset)) : ": ",
                    QString::fromStdString(diagnostic.message)));
        }
        embeddedDiagnostics->setPlainText(diagnosticText.isEmpty()
            ? "No embedded MLD diagnostics." : diagnosticText.join('\n'));

        const auto commandValues = session->commands(index);
        commands->setRowCount(static_cast<int>(commandValues.size()));
        for (int commandRow = 0; commandRow < commands->rowCount(); ++commandRow) {
            const auto& command = commandValues[static_cast<std::size_t>(commandRow)];
            commands->setItem(commandRow, 0, item(QString::number(command.index)));
            commands->setItem(commandRow, 1, item(QString::number(command.type)));
            commands->setItem(commandRow, 2, item(QString::fromStdString(command.typeLabel)));
            commands->setItem(commandRow, 3, item(QString::number(command.argument)));
            commands->setItem(commandRow, 4, item(command.localSlotIndex.has_value()
                ? QString("Local slot %1").arg(*command.localSlotIndex) : QString("—")));
            commands->setItem(commandRow, 5, item(QString::number(command.payloadSize)));
            commands->setItem(commandRow, 6, item(command.payloadInBounds ? "Valid" : "Invalid"));
        }
        if (commands->rowCount() > 0) commands->setCurrentCell(0, 0);
        else selectCommand(-1);
    }

    void selectTexture(const int row) {
        const int recordRow = records->currentRow();
        const auto textures = recordRow >= 0
            ? session->embeddedMldTextures(static_cast<std::size_t>(recordRow))
            : std::vector<spice::mix::MldTextureSnapshot>{};
        if (row < 0 || static_cast<std::size_t>(row) >= textures.size() || recordRow < 0) {
            embeddedTextureMetadata->setText("No embedded texture selected.");
            embeddedTextureViewport->setImage(std::nullopt);
            return;
        }
        const auto& texture = textures[static_cast<std::size_t>(row)];
        embeddedTextureMetadata->setText(QString(
            "<b>%1</b> — %2, %3, %4 x %5, mipmaps: %6, global index: %7, encoded: %8 bytes")
            .arg(QString::fromStdString(texture.name).toHtmlEscaped(), textureKind(texture.encoding),
                QString::fromStdString(texture.format))
            .arg(texture.width).arg(texture.height).arg(texture.mipmaps ? "Yes" : "No")
            .arg(texture.hasGlobalIndex ? QString::number(texture.globalIndex) : "None")
            .arg(texture.encodedSize));
        embeddedTextureViewport->setImage(session->embeddedMldTexturePreview(
            static_cast<std::size_t>(recordRow), texture.index));
    }

    void selectCommand(const int row) {
        const int recordRow = records->currentRow();
        if (recordRow < 0 || row < 0) {
            commandSummary->setText("No SST command selected.");
            commandFields->setRowCount(0);
            lightingRows->setRowCount(0);
            consumerWindows->clear();
            rawPayload->clear();
            return;
        }
        const auto commandValues = session->commands(static_cast<std::size_t>(recordRow));
        if (static_cast<std::size_t>(row) >= commandValues.size()) return;
        const auto detail = session->commandDetail(static_cast<std::size_t>(recordRow), commandValues[static_cast<std::size_t>(row)].index);
        if (!detail.has_value()) return;
        const auto& summary = detail->summary;
        QString slot = "No local-slot field";
        if (summary.localSlotIndex.has_value()) {
            slot = QString("Local slot %1 of current SML record %2")
                .arg(*summary.localSlotIndex).arg(recordRow);
            if (summary.localSlotRangeKnown) {
                slot += QString(" (%1 of %2)").arg(summary.localSlotInRange ? "in range" : "out of range")
                    .arg(summary.localSlotCount.value_or(0));
            }
        }
        commandSummary->setText(QString(
            "<b>Type %1 — %2</b><br>%3<br>%4<br>Record: %5 &nbsp; Payload: %6 + %7 bytes")
            .arg(summary.type).arg(QString::fromStdString(summary.typeLabel).toHtmlEscaped(),
                QString::fromStdString(summary.typeDescription).toHtmlEscaped(), slot)
            .arg(hexValue(summary.recordOffset)).arg(hexValue(summary.payloadOffset)).arg(summary.payloadSize));

        commandFields->setRowCount(static_cast<int>(detail->fields.size()));
        for (int fieldRow = 0; fieldRow < commandFields->rowCount(); ++fieldRow) {
            const auto& field = detail->fields[static_cast<std::size_t>(fieldRow)];
            commandFields->setItem(fieldRow, 0, item(hexValue(field.offset, 2)));
            commandFields->setItem(fieldRow, 1, item(QString::fromStdString(field.name)));
            commandFields->setItem(fieldRow, 2, item(field.valueAvailable ? QString::fromStdString(field.value) : "Unavailable"));
            commandFields->setItem(fieldRow, 3, item(QString::fromStdString(field.rawHex)));
            commandFields->setItem(fieldRow, 4, item(QString::fromStdString(field.width)));
            commandFields->setItem(fieldRow, 5, item(QString::fromStdString(field.kind)));
            commandFields->setItem(fieldRow, 6, item(evidenceText(field.evidence)));
            commandFields->setItem(fieldRow, 7, item(QString::fromStdString(field.scope)));
            commandFields->setItem(fieldRow, 8, item(field.provisional ? "Yes" : "No"));
            commandFields->setItem(fieldRow, 9, item(QString::fromStdString(field.description)));
        }

        lightingRows->setRowCount(static_cast<int>(detail->lightingRows.size()));
        for (int lightingRow = 0; lightingRow < lightingRows->rowCount(); ++lightingRow) {
            const auto& lighting = detail->lightingRows[static_cast<std::size_t>(lightingRow)];
            lightingRows->setItem(lightingRow, 0, item(QString::number(lighting.index)));
            lightingRows->setItem(lightingRow, 1, item(hexValue(lighting.rowOffset)));
            lightingRows->setItem(lightingRow, 2, item(QString::number(lighting.state)));
            lightingRows->setItem(lightingRow, 3, item(lighting.sentinel ? "Yes" : "No"));
            lightingRows->setItem(lightingRow, 4, item(QString::number(lighting.classSelector)));
            lightingRows->setItem(lightingRow, 5, item(hexValue(lighting.flags)));
            lightingRows->setItem(lightingRow, 6, item(QString::number(lighting.runtimeSlotId)));
            lightingRows->setItem(lightingRow, 7, item(vectorText(lighting.lightVector)));
            lightingRows->setItem(lightingRow, 8, item(vectorText(lighting.slotRgb)));
            lightingRows->setItem(lightingRow, 9, item(vectorText(lighting.globalRgb)));
        }

        QStringList windows{};
        for (const auto& window : detail->consumerWindows) {
            windows.push_back(QString("%1 @ %2, %3 bytes, %4\n%5\nRaw: %6")
                .arg(QString::fromStdString(window.name), hexValue(window.offset))
                .arg(window.size).arg(window.inBounds ? "in bounds" : "unavailable")
                .arg(QString::fromStdString(window.description), QString::fromStdString(window.rawHex)));
            for (const auto& field : window.fields) {
                windows.push_back(QString("  %1 %2 = %3 [%4; %5]")
                    .arg(hexValue(field.offset, 2), QString::fromStdString(field.name),
                        field.valueAvailable ? QString::fromStdString(field.value) : "Unavailable",
                        evidenceText(field.evidence), field.provisional ? "provisional" : "non-provisional"));
            }
        }
        consumerWindows->setPlainText(windows.isEmpty() ? "No separate consumer windows." : windows.join('\n'));
        rawPayload->setPlainText(QString("rawWord4: %1\nrawWord8: %2\nonDiskWord12: %3\n\nPayload bytes:\n%4")
            .arg(hexValue(detail->rawWord4), hexValue(detail->rawWord8), hexValue(detail->onDiskWord12),
                QString::fromStdString(detail->payloadHex)));
    }

    void populateGrid() {
        const auto value = session->battleGrid();
        if (!value.has_value()) {
            gridSummary->setText("No complete 9×9 terrain source is available in the first SST command block.");
            grid->setRowCount(0);
            gridPadding->setPlainText("No terrain padding available.");
            return;
        }
        gridSummary->setText(QString(
            "Raw first-block terrain source at %1, %2 bytes. Values are displayed without inferred terrain labels.")
            .arg(hexValue(value->sourceOffset)).arg(value->sourceSize));
        grid->setRowCount(9);
        grid->setColumnCount(9);
        for (int row = 0; row < 9; ++row) {
            for (int column = 0; column < 9; ++column) {
                auto* cell = item(QString::number(value->values[static_cast<std::size_t>(row * 9 + column)]));
                cell->setTextAlignment(Qt::AlignCenter);
                grid->setItem(row, column, cell);
            }
        }
        grid->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
        grid->verticalHeader()->setSectionResizeMode(QHeaderView::Stretch);
        gridPadding->setPlainText(QString("Padding after 9×9 source:\n%1").arg(QString::fromStdString(value->paddingHex)));
    }

    void populateRuntimeContext() {
        const auto runtime = session->runtimeContext();
        runtimeSummary->setText(QString(
            "<b>Proved active-row stride:</b> %1 bytes &nbsp; <b>Allocation width:</b> %2 bytes<br>%3")
            .arg(runtime.provedRowStride).arg(runtime.allocationWidthPerRecord)
            .arg(QString::fromStdString(runtime.allocationWidthNote).toHtmlEscaped()));
        runtimeFields->setRowCount(static_cast<int>(runtime.fields.size()));
        for (int row = 0; row < runtimeFields->rowCount(); ++row) {
            const auto& field = runtime.fields[static_cast<std::size_t>(row)];
            runtimeFields->setItem(row, 0, item(hexValue(field.offset, 2)));
            runtimeFields->setItem(row, 1, item(QString::number(field.size)));
            runtimeFields->setItem(row, 2, item(QString::fromStdString(field.name)));
            runtimeFields->setItem(row, 3, item(QString::fromStdString(field.description)));
        }
        const auto links = session->localSlotLinks();
        slotLinks->setRowCount(static_cast<int>(links.size()));
        for (int row = 0; row < slotLinks->rowCount(); ++row) {
            const auto& link = links[static_cast<std::size_t>(row)];
            slotLinks->setItem(row, 0, item(QString::number(link.recordIndex)));
            slotLinks->setItem(row, 1, item(QString::number(link.commandIndex)));
            slotLinks->setItem(row, 2, item(QString::number(link.commandType)));
            slotLinks->setItem(row, 3, item(QString::number(link.localSlotIndex)));
            slotLinks->setItem(row, 4, item(link.localSlotCount.has_value() ? QString::number(*link.localSlotCount) : "Unknown"));
            slotLinks->setItem(row, 5, item(!link.rangeKnown ? "Unknown" : link.inRange ? "In range" : "Out of range"));
        }
    }

    void populateDiagnostics() {
        const auto values = session->diagnostics();
        diagnostics->setRowCount(static_cast<int>(values.size()));
        for (int row = 0; row < diagnostics->rowCount(); ++row) {
            const auto& value = values[static_cast<std::size_t>(row)];
            diagnostics->setItem(row, 0, item(levelText(value.level)));
            diagnostics->setItem(row, 1, item(QString::fromStdString(value.origin)));
            diagnostics->setItem(row, 2, item(value.recordIndex.has_value() ? QString::number(*value.recordIndex) : "—"));
            diagnostics->setItem(row, 3, item(value.sourceOffset.has_value() ? hexValue(*value.sourceOffset) : "—"));
            diagnostics->setItem(row, 4, item(QString::fromStdString(value.message)));
        }
    }
};

SstSmlWorkbench::SstSmlWorkbench(std::shared_ptr<spice::mix::SstSmlDocumentSession> session,
    QWidget* parent)
    : DocumentWorkbench(parent), impl_(std::make_unique<Impl>()) {
    impl_->session = std::move(session);
    setObjectName("sstSmlWorkbench");
    auto* root = new QVBoxLayout(this);
    impl_->pages = new QTabWidget(this);
    impl_->pages->setObjectName("sstSmlPages");
    root->addWidget(impl_->pages);

    auto* overviewPage = new QWidget(impl_->pages);
    auto* overviewLayout = new QVBoxLayout(overviewPage);
    impl_->overview = new QLabel(overviewPage);
    impl_->overview->setWordWrap(true);
    impl_->overview->setTextInteractionFlags(Qt::TextSelectableByMouse);
    overviewLayout->addWidget(impl_->overview);
    overviewLayout->addStretch(1);
    impl_->pages->addTab(overviewPage, "Overview");

    auto* recordsPage = new QWidget(impl_->pages);
    auto* recordsLayout = new QVBoxLayout(recordsPage);
    auto* splitter = new QSplitter(Qt::Horizontal, recordsPage);
    impl_->records = new QTableWidget(splitter);
    impl_->records->setObjectName("sstSmlRecordTable");
    configureTable(impl_->records, { "Index", "MLD bytes", "MLD state", "Entries", "Textures", "Commands", "SST block", "MLD offset", "Block offset" });

    impl_->embeddedPages = new QTabWidget(splitter);
    impl_->embeddedPages->setObjectName("embeddedMldInspector");
    auto* embeddedOverviewPage = new QWidget(impl_->embeddedPages);
    auto* embeddedOverviewLayout = new QVBoxLayout(embeddedOverviewPage);
    impl_->embeddedOverview = new QLabel(embeddedOverviewPage);
    impl_->embeddedOverview->setWordWrap(true);
    impl_->embeddedOverview->setTextInteractionFlags(Qt::TextSelectableByMouse);
    embeddedOverviewLayout->addWidget(impl_->embeddedOverview);
    embeddedOverviewLayout->addStretch(1);
    impl_->embeddedPages->addTab(embeddedOverviewPage, "Overview");

    impl_->embeddedEntryInspector = new MldEntryInspector(impl_->embeddedPages);
    impl_->embeddedEntryInspector->setObjectName("embeddedMldEntryInspector");
    impl_->embeddedPages->addTab(impl_->embeddedEntryInspector, "Entries");

    auto* texturePage = new QWidget(impl_->embeddedPages);
    auto* textureLayout = new QVBoxLayout(texturePage);
    impl_->embeddedTextures = new QTableWidget(texturePage);
    configureTable(impl_->embeddedTextures, { "Index", "Name", "Kind", "Format", "Size", "State" });
    impl_->embeddedTextures->setMaximumHeight(190);
    textureLayout->addWidget(impl_->embeddedTextures);
    impl_->embeddedTextureMetadata = new QLabel(texturePage);
    impl_->embeddedTextureMetadata->setWordWrap(true);
    textureLayout->addWidget(impl_->embeddedTextureMetadata);
    impl_->embeddedTextureViewport = new TextureViewport(texturePage);
    impl_->embeddedTextureViewport->setObjectName("embeddedMldTextureViewport");
    textureLayout->addWidget(impl_->embeddedTextureViewport, 1);
    impl_->embeddedPages->addTab(texturePage, "Textures");

    impl_->embeddedDiagnostics = new QTextEdit(impl_->embeddedPages);
    impl_->embeddedDiagnostics->setReadOnly(true);
    impl_->embeddedPages->addTab(impl_->embeddedDiagnostics, "Diagnostics");

    auto* commandPanel = new QWidget(splitter);
    auto* commandLayout = new QVBoxLayout(commandPanel);
    impl_->commands = new QTableWidget(commandPanel);
    impl_->commands->setObjectName("sstCommandTable");
    configureTable(impl_->commands, { "Index", "Type", "Family", "Argument", "Target", "Bytes", "Payload" });
    impl_->commands->setMaximumHeight(230);
    commandLayout->addWidget(impl_->commands);
    impl_->commandSummary = new QLabel(commandPanel);
    impl_->commandSummary->setWordWrap(true);
    impl_->commandSummary->setTextInteractionFlags(Qt::TextSelectableByMouse);
    commandLayout->addWidget(impl_->commandSummary);
    auto* commandDetails = new QTabWidget(commandPanel);
    impl_->commandFields = new QTableWidget(commandDetails);
    configureTable(impl_->commandFields, { "Offset", "Name", "Value", "Raw", "Width", "Kind", "Evidence", "Scope", "Provisional", "Description" });
    commandDetails->addTab(impl_->commandFields, "Fields");
    impl_->lightingRows = new QTableWidget(commandDetails);
    configureTable(impl_->lightingRows, { "Index", "Offset", "State", "Sentinel", "Class", "Flags", "Runtime slot", "Light vector", "Slot RGB", "Global RGB" });
    commandDetails->addTab(impl_->lightingRows, "Lighting");
    impl_->consumerWindows = new QTextEdit(commandDetails);
    impl_->consumerWindows->setReadOnly(true);
    commandDetails->addTab(impl_->consumerWindows, "Consumer windows");
    impl_->rawPayload = new QTextEdit(commandDetails);
    impl_->rawPayload->setReadOnly(true);
    commandDetails->addTab(impl_->rawPayload, "Raw");
    commandLayout->addWidget(commandDetails, 1);

    splitter->addWidget(impl_->records);
    splitter->addWidget(impl_->embeddedPages);
    splitter->addWidget(commandPanel);
    splitter->setStretchFactor(0, 0);
    splitter->setStretchFactor(1, 1);
    splitter->setStretchFactor(2, 1);
    splitter->setSizes({ 300, 480, 500 });
    recordsLayout->addWidget(splitter);
    impl_->pages->addTab(recordsPage, "Records");

    auto* gridPage = new QWidget(impl_->pages);
    auto* gridLayout = new QVBoxLayout(gridPage);
    impl_->gridSummary = new QLabel(gridPage);
    impl_->gridSummary->setWordWrap(true);
    gridLayout->addWidget(impl_->gridSummary);
    impl_->grid = new QTableWidget(gridPage);
    impl_->grid->setEditTriggers(QAbstractItemView::NoEditTriggers);
    impl_->grid->setSelectionMode(QAbstractItemView::NoSelection);
    gridLayout->addWidget(impl_->grid, 1);
    impl_->gridPadding = new QTextEdit(gridPage);
    impl_->gridPadding->setReadOnly(true);
    impl_->gridPadding->setMaximumHeight(120);
    gridLayout->addWidget(impl_->gridPadding);
    impl_->pages->addTab(gridPage, "Battle Grid");

    auto* runtimePage = new QWidget(impl_->pages);
    auto* runtimeLayout = new QVBoxLayout(runtimePage);
    impl_->runtimeSummary = new QLabel(runtimePage);
    impl_->runtimeSummary->setWordWrap(true);
    runtimeLayout->addWidget(impl_->runtimeSummary);
    impl_->runtimeFields = new QTableWidget(runtimePage);
    configureTable(impl_->runtimeFields, { "Offset", "Size", "Runtime field", "Evidence-backed description" });
    runtimeLayout->addWidget(impl_->runtimeFields);
    auto* linksLabel = new QLabel("<b>Same-record local object-slot links</b>", runtimePage);
    runtimeLayout->addWidget(linksLabel);
    impl_->slotLinks = new QTableWidget(runtimePage);
    configureTable(impl_->slotLinks, { "Record", "Command", "Type", "Local slot", "Slot count", "Range" });
    runtimeLayout->addWidget(impl_->slotLinks);
    impl_->pages->addTab(runtimePage, "Runtime Context");

    impl_->diagnostics = new QTableWidget(impl_->pages);
    configureTable(impl_->diagnostics, { "Level", "Origin", "Record", "Offset", "Message" });
    impl_->pages->addTab(impl_->diagnostics, "Diagnostics");

    connect(impl_->records, &QTableWidget::currentCellChanged, this,
        [this](const int row) { impl_->selectRecord(row); });
    connect(impl_->embeddedTextures, &QTableWidget::currentCellChanged, this,
        [this](const int row) { impl_->selectTexture(row); });
    connect(impl_->commands, &QTableWidget::currentCellChanged, this,
        [this](const int row) { impl_->selectCommand(row); });

    impl_->populateOverview();
    impl_->populateGrid();
    impl_->populateRuntimeContext();
    impl_->populateDiagnostics();
    impl_->populateRecords();
}

SstSmlWorkbench::~SstSmlWorkbench() = default;

QString SstSmlWorkbench::displayName() const {
    return QString::fromStdString(impl_->session->overview().stem) + " — SST/SML";
}

std::vector<std::filesystem::path> SstSmlWorkbench::sourcePaths() const {
    return impl_->session->sourcePaths();
}

bool SstSmlWorkbench::runSmokeChecks() {
    const QStringList expected{ "Overview", "Records", "Battle Grid", "Runtime Context", "Diagnostics" };
    bool pagesOk = impl_->pages->count() == expected.size();
    for (int index = 0; pagesOk && index < expected.size(); ++index) {
        pagesOk = impl_->pages->tabText(index) == expected[index];
    }
    const auto recordValues = impl_->session->records();
    const bool recordSync = impl_->records->rowCount() == static_cast<int>(recordValues.size());
    const bool commandSync = impl_->records->currentRow() < 0
        || impl_->commands->rowCount() == static_cast<int>(impl_->session->commands(
            static_cast<std::size_t>(impl_->records->currentRow())).size());
    const bool entriesReady = impl_->embeddedEntryInspector
        && impl_->embeddedEntryInspector->runSmokeChecks();
    return objectName() == "sstSmlWorkbench" && pagesOk && recordSync && commandSync && entriesReady
        && !dirty() && !canSaveAs()
        && !impl_->embeddedTextureViewport->hasFileDropHandler()
        && impl_->embeddedTextureViewport->samplingMode() == TextureViewport::SamplingMode::Nearest
        && impl_->embeddedTextureViewport->zoomMode() == TextureViewport::ZoomMode::IntegerFit
        && impl_->embeddedTextureViewport->verifyViewControlsDoNotInvoke([this]() { return dirty(); });
}

void SstSmlWorkbench::requestSaveAs(std::function<void(bool)> completed) {
    if (completed) completed(false);
}
