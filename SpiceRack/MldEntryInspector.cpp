#include "MldEntryInspector.h"

#include <QtCore/QStringList>
#include <QtWidgets/QAbstractItemView>
#include <QtWidgets/QHeaderView>
#include <QtWidgets/QLabel>
#include <QtWidgets/QScrollArea>
#include <QtWidgets/QSplitter>
#include <QtWidgets/QTableWidget>
#include <QtWidgets/QTreeWidget>
#include <QtWidgets/QVBoxLayout>

#include <algorithm>
#include <array>

namespace {

QString hexValue(const std::uint32_t value) {
    return QString("0x%1").arg(value, 8, 16, QChar('0')).toUpper();
}

QString validityText(const bool valid) {
    return valid ? "Valid" : "Invalid";
}

QTableWidgetItem* readOnlyItem(const QString& value) {
    auto* result = new QTableWidgetItem(value);
    result->setFlags(result->flags() & ~Qt::ItemIsEditable);
    return result;
}

QString addressValuesText(const spice::mix::MldU32ListSnapshot& list) {
    if (list.values.empty()) return "Empty";
    QStringList values{};
    values.reserve(static_cast<qsizetype>(list.values.size()));
    for (std::size_t index = 0; index < list.values.size(); ++index) {
        values.push_back(QString("[%1] %2 (%3)")
            .arg(index).arg(hexValue(list.values[index])).arg(list.values[index]));
    }
    return values.join(", ");
}

std::size_t nonzeroCount(const spice::mix::MldU32ListSnapshot& list) {
    return static_cast<std::size_t>(std::count_if(list.values.begin(), list.values.end(),
        [](const auto value) { return value != 0U; }));
}

} // namespace

struct MldEntryInspector::Impl {
    QSplitter* splitter = nullptr;
    QTableWidget* table = nullptr;
    QScrollArea* detailScroll = nullptr;
    QWidget* detailBody = nullptr;
    QLabel* placeholder = nullptr;
    QLabel* scalarDetails = nullptr;
    QTreeWidget* expandableLists = nullptr;
    QLabel* resourceAddressesHeading = nullptr;
    QLabel* objectAddresses = nullptr;
    QLabel* groundAddresses = nullptr;
    QLabel* motionAddresses = nullptr;
    std::vector<spice::mix::MldEntryDetailSnapshot> entries{};
    std::size_t displayedTableIndex = static_cast<std::size_t>(-1);

    void clearDetails() {
        displayedTableIndex = static_cast<std::size_t>(-1);
        placeholder->show();
        scalarDetails->clear();
        scalarDetails->hide();
        expandableLists->clear();
        expandableLists->hide();
        resourceAddressesHeading->hide();
        objectAddresses->clear();
        groundAddresses->clear();
        motionAddresses->clear();
        objectAddresses->hide();
        groundAddresses->hide();
        motionAddresses->hide();
    }

    QTreeWidgetItem* addNumericGroup(const QString& name,
        const spice::mix::MldU32ListSnapshot& list, const QString& interpretation,
        const bool expanded) {
        auto* group = new QTreeWidgetItem(expandableLists);
        group->setText(0, name);
        group->setText(1, hexValue(list.pointer));
        group->setText(2, QString::number(list.values.size()));
        group->setText(3, validityText(list.valid));
        for (std::size_t index = 0; index < list.values.size(); ++index) {
            auto* child = new QTreeWidgetItem(group);
            child->setText(0, QString("[%1]").arg(index));
            child->setText(1, hexValue(list.values[index]));
            child->setText(2, QString::number(list.values[index]));
            child->setText(3, interpretation);
        }
        group->setExpanded(expanded);
        return group;
    }

    QTreeWidgetItem* addTextureGroup(const spice::mix::MldStringListSnapshot& list,
        const bool expanded) {
        auto* group = new QTreeWidgetItem(expandableLists);
        group->setText(0, "Texture Names");
        group->setText(1, hexValue(list.pointer));
        group->setText(2, QString::number(list.values.size()));
        group->setText(3, validityText(list.valid));
        for (std::size_t index = 0; index < list.values.size(); ++index) {
            auto* child = new QTreeWidgetItem(group);
            child->setText(0, QString("[%1]").arg(index));
            child->setText(3, QString::fromStdString(list.values[index]));
        }
        group->setExpanded(expanded);
        return group;
    }

    void setAddressLabel(QLabel* label, const QString& name,
        const spice::mix::MldU32ListSnapshot& list) {
        label->setText(QString(
            "<b>%1</b> — pointer %2 — %3 values / %4 nonzero — %5<br>"
            "<span style=\"font-family: monospace\">%6</span>")
            .arg(name, hexValue(list.pointer))
            .arg(list.values.size()).arg(nonzeroCount(list)).arg(validityText(list.valid))
            .arg(addressValuesText(list).toHtmlEscaped()));
        label->show();
    }

    void showDetails(const int row) {
        if (row < 0 || static_cast<std::size_t>(row) >= entries.size()) {
            clearDetails();
            return;
        }
        std::array<bool, 4> expanded{};
        for (int index = 0; index < std::min(4, expandableLists->topLevelItemCount()); ++index) {
            expanded[static_cast<std::size_t>(index)] = expandableLists->topLevelItem(index)->isExpanded();
        }

        const auto& detail = entries[static_cast<std::size_t>(row)];
        const auto& entry = detail.summary;
        displayedTableIndex = entry.tableIndex;
        placeholder->hide();
        scalarDetails->setText(QString(
            "<h3>Entry %1</h3>"
            "<p><b>Entry ID:</b> %2 &nbsp; <b>Table ID:</b> %3 &nbsp; <b>Function:</b> %4</p>"
            "<p><b>Position:</b> (%5, %6, %7)<br>"
            "<b>Raw rotation:</b> (%8, %9, %10)<br>"
            "<b>Scale:</b> (%11, %12, %13)</p>"
            "<p><b>Nonzero resources:</b> %14 objects, %15 ground, %16 motions<br>"
            "<b>Ground Links pointer:</b> %17 &nbsp; <b>Param List 2 pointer:</b> %18<br>"
            "<b>Function Parameters pointer:</b> %19<br>"
            "<b>Object Addresses pointer:</b> %20 &nbsp; <b>Ground Addresses pointer:</b> %21<br>"
            "<b>Motion Addresses pointer:</b> %22 &nbsp; <b>Texture Names pointer:</b> %23</p>")
            .arg(entry.tableIndex).arg(entry.entryId).arg(entry.tableId)
            .arg(QString::fromStdString(entry.functionName).toHtmlEscaped())
            .arg(entry.positionX).arg(entry.positionY).arg(entry.positionZ)
            .arg(entry.rotationX).arg(entry.rotationY).arg(entry.rotationZ)
            .arg(entry.scaleX).arg(entry.scaleY).arg(entry.scaleZ)
            .arg(entry.objectCount).arg(entry.groundCount).arg(entry.motionCount)
            .arg(hexValue(detail.groundLinks.pointer)).arg(hexValue(detail.paramList2.pointer))
            .arg(hexValue(detail.functionParameters.pointer))
            .arg(hexValue(detail.objectAddresses.pointer)).arg(hexValue(detail.groundAddresses.pointer))
            .arg(hexValue(detail.motionAddresses.pointer)).arg(hexValue(detail.textureNames.pointer)));
        scalarDetails->show();

        expandableLists->clear();
        addNumericGroup("Ground Links", detail.groundLinks, "Ground-link value", expanded[0]);
        addNumericGroup("Param List 2", detail.paramList2, "Param List 2 value", expanded[1]);
        addNumericGroup("Function Parameters", detail.functionParameters, "Function parameter", expanded[2]);
        addTextureGroup(detail.textureNames, expanded[3]);
        expandableLists->show();

        resourceAddressesHeading->show();
        setAddressLabel(objectAddresses, "Object Addresses", detail.objectAddresses);
        setAddressLabel(groundAddresses, "Ground Addresses", detail.groundAddresses);
        setAddressLabel(motionAddresses, "Motion Addresses", detail.motionAddresses);
    }
};

MldEntryInspector::MldEntryInspector(QWidget* parent)
    : QWidget(parent), impl_(std::make_unique<Impl>()) {
    setObjectName("mldEntryInspector");
    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    impl_->splitter = new QSplitter(Qt::Horizontal, this);
    impl_->splitter->setObjectName("mldEntryInspectorSplitter");
    root->addWidget(impl_->splitter);

    impl_->table = new QTableWidget(impl_->splitter);
    impl_->table->setObjectName("mldEntryInspectorTable");
    impl_->table->setColumnCount(4);
    impl_->table->setHorizontalHeaderLabels({ "Index", "Entry ID", "Table ID", "Function" });
    impl_->table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    impl_->table->setSelectionBehavior(QAbstractItemView::SelectRows);
    impl_->table->setSelectionMode(QAbstractItemView::SingleSelection);
    impl_->table->setAlternatingRowColors(true);
    impl_->table->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
    impl_->table->horizontalHeader()->setStretchLastSection(true);
    impl_->table->setMinimumWidth(300);

    impl_->detailScroll = new QScrollArea(impl_->splitter);
    impl_->detailScroll->setObjectName("mldEntryInspectorDetails");
    impl_->detailScroll->setWidgetResizable(true);
    impl_->detailBody = new QWidget(impl_->detailScroll);
    auto* detailsLayout = new QVBoxLayout(impl_->detailBody);
    impl_->placeholder = new QLabel("Select an MLD entry to inspect its linked lists.", impl_->detailBody);
    impl_->placeholder->setAlignment(Qt::AlignCenter);
    detailsLayout->addWidget(impl_->placeholder);
    impl_->scalarDetails = new QLabel(impl_->detailBody);
    impl_->scalarDetails->setObjectName("mldEntryScalarDetails");
    impl_->scalarDetails->setWordWrap(true);
    impl_->scalarDetails->setTextInteractionFlags(Qt::TextSelectableByMouse);
    detailsLayout->addWidget(impl_->scalarDetails);

    impl_->expandableLists = new QTreeWidget(impl_->detailBody);
    impl_->expandableLists->setObjectName("mldEntryExpandableLists");
    impl_->expandableLists->setColumnCount(4);
    impl_->expandableLists->setHeaderLabels({ "List / slot", "Pointer / hex", "Count / decimal", "State / interpretation" });
    impl_->expandableLists->setEditTriggers(QAbstractItemView::NoEditTriggers);
    impl_->expandableLists->setSelectionMode(QAbstractItemView::NoSelection);
    impl_->expandableLists->setMinimumHeight(250);
    impl_->expandableLists->header()->setSectionResizeMode(QHeaderView::ResizeToContents);
    impl_->expandableLists->header()->setStretchLastSection(true);
    detailsLayout->addWidget(impl_->expandableLists);

    impl_->resourceAddressesHeading = new QLabel("<h3>Resource Addresses</h3>", impl_->detailBody);
    impl_->resourceAddressesHeading->setObjectName("mldResourceAddressesHeading");
    detailsLayout->addWidget(impl_->resourceAddressesHeading);

    auto makeAddressLabel = [body = impl_->detailBody, detailsLayout](const char* objectName) {
        auto* label = new QLabel(body);
        label->setObjectName(objectName);
        label->setWordWrap(true);
        label->setTextInteractionFlags(Qt::TextSelectableByMouse);
        label->setMargin(4);
        detailsLayout->addWidget(label);
        return label;
    };
    impl_->objectAddresses = makeAddressLabel("mldObjectAddresses");
    impl_->groundAddresses = makeAddressLabel("mldGroundAddresses");
    impl_->motionAddresses = makeAddressLabel("mldMotionAddresses");
    detailsLayout->addStretch(1);
    impl_->detailScroll->setWidget(impl_->detailBody);

    impl_->splitter->addWidget(impl_->table);
    impl_->splitter->addWidget(impl_->detailScroll);
    impl_->splitter->setStretchFactor(0, 2);
    impl_->splitter->setStretchFactor(1, 3);
    impl_->splitter->setSizes({ 420, 650 });

    connect(impl_->table, &QTableWidget::currentCellChanged, this,
        [this](const int row) { impl_->showDetails(row); });
    impl_->clearDetails();
}

MldEntryInspector::~MldEntryInspector() = default;

void MldEntryInspector::setEntries(std::vector<spice::mix::MldEntryDetailSnapshot> entries) {
    impl_->entries = std::move(entries);
    impl_->table->clearContents();
    impl_->table->setRowCount(static_cast<int>(impl_->entries.size()));
    for (int row = 0; row < impl_->table->rowCount(); ++row) {
        const auto& entry = impl_->entries[static_cast<std::size_t>(row)].summary;
        impl_->table->setItem(row, 0, readOnlyItem(QString::number(entry.tableIndex)));
        impl_->table->setItem(row, 1, readOnlyItem(QString::number(entry.entryId)));
        impl_->table->setItem(row, 2, readOnlyItem(QString::number(entry.tableId)));
        impl_->table->setItem(row, 3, readOnlyItem(QString::fromStdString(entry.functionName)));
    }
    if (impl_->entries.empty()) {
        impl_->table->setCurrentCell(-1, -1);
        impl_->clearDetails();
    } else {
        impl_->table->setCurrentCell(0, 0);
        impl_->showDetails(0);
    }
}

bool MldEntryInspector::runSmokeChecks() {
    if (!impl_->splitter || impl_->splitter->count() != 2 || impl_->table->columnCount() != 4
        || impl_->table->rowCount() != static_cast<int>(impl_->entries.size())) return false;
    if (impl_->entries.empty()) return !impl_->placeholder->isHidden() && impl_->expandableLists->isHidden();
    if (impl_->table->currentRow() < 0 || impl_->expandableLists->topLevelItemCount() != 4
        || impl_->displayedTableIndex != impl_->entries[static_cast<std::size_t>(impl_->table->currentRow())].summary.tableIndex) {
        return false;
    }
    for (int index = 0; index < impl_->expandableLists->topLevelItemCount(); ++index) {
        if (impl_->expandableLists->topLevelItem(index)->isExpanded()) return false;
    }
    const int originalRow = impl_->table->currentRow();
    const int checkRow = impl_->table->rowCount() > 1 ? 1 : 0;
    impl_->table->setCurrentCell(checkRow, 0);
    const bool selectionUpdated = impl_->displayedTableIndex
        == impl_->entries[static_cast<std::size_t>(checkRow)].summary.tableIndex;
    auto* firstGroup = impl_->expandableLists->topLevelItem(0);
    firstGroup->setExpanded(true);
    const bool expanded = firstGroup->isExpanded();
    firstGroup->setExpanded(false);
    impl_->table->setCurrentCell(originalRow, 0);
    return selectionUpdated && expanded
        && !impl_->objectAddresses->text().isEmpty()
        && !impl_->groundAddresses->text().isEmpty()
        && !impl_->motionAddresses->text().isEmpty();
}
