#include "DocumentWorkbenches.h"

#include <QtCore/QStringList>
#include <QtGui/QFont>
#include <QtWidgets/QAbstractItemView>
#include <QtWidgets/QHeaderView>
#include <QtWidgets/QLabel>
#include <QtWidgets/QLineEdit>
#include <QtWidgets/QSplitter>
#include <QtWidgets/QTabWidget>
#include <QtWidgets/QTableWidget>
#include <QtWidgets/QTreeWidget>
#include <QtWidgets/QVBoxLayout>

#include <algorithm>

namespace {

constexpr int kItemKindRole = Qt::UserRole;
constexpr int kItemIndexRole = Qt::UserRole + 1;
constexpr int kSearchTextRole = Qt::UserRole + 2;
constexpr int kEntryItem = 0;
constexpr int kTableItem = 1;

QString qpath(const std::filesystem::path& path) {
    return QString::fromStdWString(path.wstring());
}

QString hexValue(const std::uint64_t value, const int width = 4) {
    return QString("0x%1").arg(value, width, 16, QChar('0')).toUpper();
}

QString number(const std::size_t value) {
    return QString::number(static_cast<qulonglong>(value));
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
    table->clear();
    table->setColumnCount(headers.size());
    table->setHorizontalHeaderLabels(headers);
    table->setSelectionBehavior(QAbstractItemView::SelectRows);
    table->setSelectionMode(QAbstractItemView::SingleSelection);
    table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table->setAlternatingRowColors(true);
    table->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
    table->horizontalHeader()->setStretchLastSection(true);
}

QString tableSearchText(const spice::mix::EctTableDetailSnapshot& table) {
    QStringList values{
        QString::fromStdString(table.summary.containerTitle),
        number(table.summary.index),
        QString::number(table.summary.stage),
        hexValue(table.summary.stage),
        QString::number(table.summary.overallEncounterRate),
        hexValue(table.summary.overallEncounterRate),
    };
    for (const auto& encounter : table.encounters) {
        values.push_back(QString::number(encounter.encounterId));
        values.push_back(hexValue(encounter.encounterId));
    }
    return values.join(' ').toCaseFolded();
}

} // namespace

struct EctWorkbench::Impl {
    std::shared_ptr<spice::mix::EctDocumentSession> session{};
    QTabWidget* pages = nullptr;
    QLabel* overview = nullptr;
    QLineEdit* filter = nullptr;
    QTreeWidget* navigation = nullptr;
    QLabel* selectionSummary = nullptr;
    QTableWidget* details = nullptr;
    QTableWidget* diagnostics = nullptr;

    void populateOverview() {
        const auto value = session->overview();
        overview->setText(QString(
            "<h3>%1</h3><p><b>Read-only encounter-table inspection</b></p>"
            "<p><b>Source:</b> %2</p>"
            "<p><b>Layout:</b> %3<br><b>Platform encoding:</b> %4 — %5, %6</p>"
            "<p>Source/decoded size: %7 / %8 bytes<br>"
            "Indexed entries: %9 &nbsp; Encounter tables: %10<br>"
            "Encounter slots: %11 &nbsp; Nonzero slots: %12</p>%13")
            .arg(qpath(value.sourcePath.filename()).toHtmlEscaped(),
                qpath(value.sourcePath).toHtmlEscaped(),
                QString::fromStdString(value.layout).toHtmlEscaped(),
                QString::fromStdString(value.platform).toHtmlEscaped(),
                value.sourceWasAklz ? "AKLZ" : "Raw",
                QString::fromStdString(value.endian).toHtmlEscaped())
            .arg(value.sourceSize).arg(value.decodedSize)
            .arg(value.containerEntryCount).arg(value.tableCount)
            .arg(value.encounterSlotCount).arg(value.nonzeroEncounterSlotCount)
            .arg(value.containerEntryCount > 0U
                ? "<p><i>Each indexed entry contains eight ordered tables. Their individual game-level roles are not yet named.</i></p>"
                : ""));
    }

    QTreeWidgetItem* makeTableItem(const spice::mix::EctTableSummarySnapshot& summary) {
        const auto label = summary.tableIndexWithinEntry.has_value()
            ? QString("Table %1").arg(*summary.tableIndexWithinEntry)
            : QString("Table %1").arg(summary.index);
        auto* result = new QTreeWidgetItem({ label, hexValue(summary.stage),
            hexValue(summary.overallEncounterRate), number(summary.nonzeroEncounterSlotCount) });
        result->setData(0, kItemKindRole, kTableItem);
        result->setData(0, kItemIndexRole, static_cast<qulonglong>(summary.index));
        const auto detail = session->table(summary.index);
        result->setData(0, kSearchTextRole,
            detail.has_value() ? tableSearchText(*detail) : label.toCaseFolded());
        return result;
    }

    void populateNavigation() {
        navigation->clear();
        const auto entries = session->containerEntries();
        const auto tables = session->tables();
        if (entries.empty()) {
            for (const auto& table : tables) navigation->addTopLevelItem(makeTableItem(table));
        } else {
            for (const auto& entry : entries) {
                auto* parent = new QTreeWidgetItem({ QString("%1 (%2 tables)")
                    .arg(QString::fromStdString(entry.title)).arg(entry.tableIndexes.size()), {}, {}, {} });
                parent->setData(0, kItemKindRole, kEntryItem);
                parent->setData(0, kItemIndexRole, static_cast<qulonglong>(entry.index));
                parent->setData(0, kSearchTextRole, QString::fromStdString(entry.title).toCaseFolded());
                QFont font = parent->font(0);
                font.setBold(true);
                parent->setFont(0, font);
                for (const auto tableIndex : entry.tableIndexes) {
                    if (tableIndex < tables.size()) parent->addChild(makeTableItem(tables[tableIndex]));
                }
                navigation->addTopLevelItem(parent);
                parent->setExpanded(entry.index == 0U);
            }
        }
        QTreeWidgetItem* initial = nullptr;
        if (navigation->topLevelItemCount() > 0) {
            auto* first = navigation->topLevelItem(0);
            initial = first->childCount() > 0 ? first->child(0) : first;
        }
        if (initial) navigation->setCurrentItem(initial);
        else showPlaceholder();
    }

    void showPlaceholder() {
        selectionSummary->setText("No encounter table is selected.");
        configureTable(details, { "Value" });
        details->setRowCount(0);
    }

    void selectEntry(const std::size_t entryIndex) {
        const auto entries = session->containerEntries();
        if (entryIndex >= entries.size()) {
            showPlaceholder();
            return;
        }
        const auto& entry = entries[entryIndex];
        selectionSummary->setText(QString(
            "<b>%1</b><br>Indexed entry %2 contains %3 ordered encounter tables. "
            "The individual roles of those eight tables are not yet named.")
            .arg(QString::fromStdString(entry.title).toHtmlEscaped())
            .arg(entry.index).arg(entry.tableIndexes.size()));
        configureTable(details, { "Table", "Stage", "Overall rate", "Nonzero slots", "Raw rate sum" });
        details->setRowCount(static_cast<int>(entry.tableIndexes.size()));
        for (int row = 0; row < details->rowCount(); ++row) {
            const auto tableIndex = entry.tableIndexes[static_cast<std::size_t>(row)];
            const auto table = session->table(tableIndex);
            if (!table.has_value()) continue;
            details->setItem(row, 0, item(QString::number(row)));
            details->setItem(row, 1, item(QString("%1 (%2)")
                .arg(hexValue(table->summary.stage)).arg(table->summary.stage)));
            details->setItem(row, 2, item(QString("%1 (%2)")
                .arg(hexValue(table->summary.overallEncounterRate)).arg(table->summary.overallEncounterRate)));
            details->setItem(row, 3, item(number(table->summary.nonzeroEncounterSlotCount)));
            details->setItem(row, 4, item(QString::number(table->summary.encounterRateSum)));
        }
    }

    void selectTable(const std::size_t tableIndex) {
        const auto table = session->table(tableIndex);
        if (!table.has_value()) {
            showPlaceholder();
            return;
        }
        QString identity = QString("Table %1").arg(table->summary.index);
        if (table->summary.tableIndexWithinEntry.has_value()) {
            identity = QString("%1 — Table %2")
                .arg(QString::fromStdString(table->summary.containerTitle).toHtmlEscaped())
                .arg(*table->summary.tableIndexWithinEntry);
        }
        selectionSummary->setText(QString(
            "<b>%1</b><br>Stage: %2 (%3) &nbsp; Overall encounter rate: %4 (%5)<br>"
            "Nonzero slots: %6 / %7 &nbsp; Raw encounter-rate sum: %8")
            .arg(identity, hexValue(table->summary.stage)).arg(table->summary.stage)
            .arg(hexValue(table->summary.overallEncounterRate)).arg(table->summary.overallEncounterRate)
            .arg(table->summary.nonzeroEncounterSlotCount).arg(table->encounters.size())
            .arg(table->summary.encounterRateSum));
        configureTable(details,
            { "Slot", "Encounter ID (hex)", "Encounter ID (decimal)", "Rate (hex)", "Rate (decimal)" });
        details->setRowCount(static_cast<int>(table->encounters.size()));
        for (int row = 0; row < details->rowCount(); ++row) {
            const auto& encounter = table->encounters[static_cast<std::size_t>(row)];
            details->setItem(row, 0, item(number(encounter.index)));
            details->setItem(row, 1, item(hexValue(encounter.encounterId)));
            details->setItem(row, 2, item(QString::number(encounter.encounterId)));
            details->setItem(row, 3, item(hexValue(encounter.encounterRate)));
            details->setItem(row, 4, item(QString::number(encounter.encounterRate)));
        }
    }

    void selectNavigationItem(QTreeWidgetItem* selected) {
        if (!selected) {
            showPlaceholder();
            return;
        }
        const auto index = static_cast<std::size_t>(selected->data(0, kItemIndexRole).toULongLong());
        if (selected->data(0, kItemKindRole).toInt() == kEntryItem) selectEntry(index);
        else selectTable(index);
    }

    void applyFilter(const QString& text) {
        const auto wanted = text.trimmed().toCaseFolded();
        for (int topIndex = 0; topIndex < navigation->topLevelItemCount(); ++topIndex) {
            auto* top = navigation->topLevelItem(topIndex);
            if (top->childCount() == 0) {
                top->setHidden(!wanted.isEmpty()
                    && !top->data(0, kSearchTextRole).toString().contains(wanted));
                continue;
            }
            const bool parentMatches = wanted.isEmpty()
                || top->data(0, kSearchTextRole).toString().contains(wanted);
            bool anyChild = false;
            for (int childIndex = 0; childIndex < top->childCount(); ++childIndex) {
                auto* child = top->child(childIndex);
                const bool matches = parentMatches
                    || child->data(0, kSearchTextRole).toString().contains(wanted);
                child->setHidden(!matches);
                anyChild = anyChild || matches;
            }
            top->setHidden(!parentMatches && !anyChild);
            if (!wanted.isEmpty() && anyChild) top->setExpanded(true);
        }
    }

    void populateDiagnostics() {
        const auto values = session->diagnostics();
        diagnostics->setRowCount(static_cast<int>(values.size()));
        for (int row = 0; row < diagnostics->rowCount(); ++row) {
            const auto& value = values[static_cast<std::size_t>(row)];
            diagnostics->setItem(row, 0, item(levelText(value.level)));
            diagnostics->setItem(row, 1, item(value.decodedOffset.has_value()
                ? hexValue(*value.decodedOffset, 8) : "—"));
            diagnostics->setItem(row, 2, item(QString::fromStdString(value.message)));
        }
    }
};

EctWorkbench::EctWorkbench(
    std::shared_ptr<spice::mix::EctDocumentSession> session,
    QWidget* parent)
    : DocumentWorkbench(parent), impl_(std::make_unique<Impl>()) {
    impl_->session = std::move(session);
    setObjectName("ectWorkbench");
    auto* root = new QVBoxLayout(this);
    impl_->pages = new QTabWidget(this);
    impl_->pages->setObjectName("ectPages");
    root->addWidget(impl_->pages);

    auto* overviewPage = new QWidget(impl_->pages);
    auto* overviewLayout = new QVBoxLayout(overviewPage);
    impl_->overview = new QLabel(overviewPage);
    impl_->overview->setObjectName("ectOverview");
    impl_->overview->setWordWrap(true);
    impl_->overview->setTextInteractionFlags(Qt::TextSelectableByMouse);
    overviewLayout->addWidget(impl_->overview);
    overviewLayout->addStretch(1);
    impl_->pages->addTab(overviewPage, "Overview");

    auto* encounterPage = new QWidget(impl_->pages);
    auto* encounterLayout = new QVBoxLayout(encounterPage);
    auto* splitter = new QSplitter(Qt::Horizontal, encounterPage);
    splitter->setObjectName("ectEncounterSplitter");
    auto* navigationPanel = new QWidget(splitter);
    auto* navigationLayout = new QVBoxLayout(navigationPanel);
    navigationLayout->setContentsMargins(0, 0, 0, 0);
    impl_->filter = new QLineEdit(navigationPanel);
    impl_->filter->setObjectName("ectFilter");
    impl_->filter->setPlaceholderText("Filter titles, stages, or encounter IDs");
    navigationLayout->addWidget(impl_->filter);
    impl_->navigation = new QTreeWidget(navigationPanel);
    impl_->navigation->setObjectName("ectNavigation");
    impl_->navigation->setColumnCount(4);
    impl_->navigation->setHeaderLabels({ "Entry / Table", "Stage", "Overall", "Nonzero" });
    impl_->navigation->setSelectionMode(QAbstractItemView::SingleSelection);
    impl_->navigation->setEditTriggers(QAbstractItemView::NoEditTriggers);
    impl_->navigation->setAlternatingRowColors(true);
    impl_->navigation->header()->setSectionResizeMode(QHeaderView::ResizeToContents);
    impl_->navigation->header()->setStretchLastSection(true);
    navigationLayout->addWidget(impl_->navigation);

    auto* detailPanel = new QWidget(splitter);
    auto* detailLayout = new QVBoxLayout(detailPanel);
    detailLayout->setContentsMargins(0, 0, 0, 0);
    impl_->selectionSummary = new QLabel(detailPanel);
    impl_->selectionSummary->setObjectName("ectSelectionSummary");
    impl_->selectionSummary->setWordWrap(true);
    impl_->selectionSummary->setTextInteractionFlags(Qt::TextSelectableByMouse);
    detailLayout->addWidget(impl_->selectionSummary);
    impl_->details = new QTableWidget(detailPanel);
    impl_->details->setObjectName("ectEncounterDetails");
    detailLayout->addWidget(impl_->details);

    splitter->addWidget(navigationPanel);
    splitter->addWidget(detailPanel);
    splitter->setStretchFactor(0, 0);
    splitter->setStretchFactor(1, 1);
    splitter->setSizes({ 360, 800 });
    encounterLayout->addWidget(splitter);
    impl_->pages->addTab(encounterPage, "Encounter Tables");

    impl_->diagnostics = new QTableWidget(impl_->pages);
    impl_->diagnostics->setObjectName("ectDiagnostics");
    configureTable(impl_->diagnostics, { "Level", "Decoded offset", "Message" });
    impl_->pages->addTab(impl_->diagnostics, "Diagnostics");

    connect(impl_->navigation, &QTreeWidget::currentItemChanged, this,
        [this](QTreeWidgetItem* current) { impl_->selectNavigationItem(current); });
    connect(impl_->filter, &QLineEdit::textChanged, this,
        [this](const QString& text) { impl_->applyFilter(text); });

    impl_->populateOverview();
    impl_->populateDiagnostics();
    impl_->populateNavigation();
}

EctWorkbench::~EctWorkbench() = default;

QString EctWorkbench::displayName() const {
    return qpath(impl_->session->overview().sourcePath.filename()) + " — ECT";
}

std::vector<std::filesystem::path> EctWorkbench::sourcePaths() const {
    return impl_->session->sourcePaths();
}

bool EctWorkbench::runSmokeChecks() {
    const QStringList expected{ "Overview", "Encounter Tables", "Diagnostics" };
    bool pagesOk = impl_->pages->count() == expected.size();
    for (int index = 0; pagesOk && index < expected.size(); ++index) {
        pagesOk = impl_->pages->tabText(index) == expected[index];
    }
    const auto overview = impl_->session->overview();
    const auto entries = impl_->session->containerEntries();
    const auto tables = impl_->session->tables();
    const int expectedTopLevelCount = static_cast<int>(entries.empty() ? tables.size() : entries.size());
    const bool navigationSync = impl_->navigation->topLevelItemCount() == expectedTopLevelCount;
    const bool detailSync = tables.empty() || (impl_->navigation->currentItem()
        && impl_->navigation->currentItem()->data(0, kItemKindRole).toInt() == kTableItem
        && impl_->details->rowCount() == 32);
    const bool filterIsViewOnly = [this]() {
        impl_->filter->setText("0xFFFF");
        impl_->filter->clear();
        return !dirty();
    }();
    return objectName() == "ectWorkbench" && pagesOk && navigationSync && detailSync
        && overview.tableCount == tables.size() && filterIsViewOnly && !dirty() && !canSaveAs();
}

void EctWorkbench::requestSaveAs(std::function<void(bool)> completed) {
    if (completed) completed(false);
}
