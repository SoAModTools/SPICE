#pragma once

#include "TaskController.h"
#include "../SpiceMix/Documents/GvrDocumentSession.h"
#include "../SpiceMix/Documents/MldDocumentSession.h"

#include <QtWidgets/QWidget>

#include <filesystem>
#include <functional>
#include <memory>
#include <optional>

class DocumentWorkbench : public QWidget {
public:
    using QWidget::QWidget;
    ~DocumentWorkbench() override = default;

    [[nodiscard]] virtual QString displayName() const = 0;
    [[nodiscard]] virtual bool dirty() const = 0;
    [[nodiscard]] virtual std::optional<std::filesystem::path> sourcePath() const = 0;
    virtual void requestSaveAs(std::function<void(bool)> completed = {}) = 0;

    void setStateChanged(std::function<void()> callback) { stateChanged_ = std::move(callback); }

protected:
    void notifyStateChanged() { if (stateChanged_) stateChanged_(); }

private:
    std::function<void()> stateChanged_{};
};

class MldWorkbench final : public DocumentWorkbench {
public:
    MldWorkbench(std::shared_ptr<spice::mix::MldDocumentSession> session,
        RackTaskController& tasks, QWidget* parent = nullptr);
    ~MldWorkbench() override;

    [[nodiscard]] QString displayName() const override;
    [[nodiscard]] bool dirty() const override;
    [[nodiscard]] std::optional<std::filesystem::path> sourcePath() const override;
    void requestSaveAs(std::function<void(bool)> completed = {}) override;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

class GvrWorkbench final : public DocumentWorkbench {
public:
    GvrWorkbench(std::shared_ptr<spice::mix::GvrDocumentSession> session,
        RackTaskController& tasks, QWidget* parent = nullptr);
    ~GvrWorkbench() override;

    [[nodiscard]] QString displayName() const override;
    [[nodiscard]] bool dirty() const override;
    [[nodiscard]] std::optional<std::filesystem::path> sourcePath() const override;
    void requestSaveAs(std::function<void(bool)> completed = {}) override;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
