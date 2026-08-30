#pragma once

#include "../SpiceMix/Documents/MldDocumentSession.h"

#include <QtWidgets/QWidget>

#include <memory>
#include <vector>

class MldEntryInspector final : public QWidget {
public:
    explicit MldEntryInspector(QWidget* parent = nullptr);
    ~MldEntryInspector() override;

    void setEntries(std::vector<spice::mix::MldEntryDetailSnapshot> entries);
    [[nodiscard]] bool runSmokeChecks();

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
