#pragma once

#include "../SpiceMix/Application/OperationRunner.h"

#include <QtWidgets/QMainWindow>

class SpiceRackMainWindow final : public QMainWindow {
public:
    explicit SpiceRackMainWindow(QWidget* parent = nullptr);

private:
    spice::mix::OperationRunner operationRunner_{};
};
