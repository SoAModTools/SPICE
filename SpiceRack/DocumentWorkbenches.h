#pragma once

#include "TaskController.h"
#include "../SpiceMix/Documents/EctDocumentSession.h"
#include "../SpiceMix/Documents/GvrDocumentSession.h"
#include "../SpiceMix/Documents/MldDocumentSession.h"
#include "../SpiceMix/Documents/PvrDocumentSession.h"
#include "../SpiceMix/Documents/SstSmlDocumentSession.h"

#include <QtWidgets/QWidget>

#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <vector>

class DocumentWorkbench : public QWidget {
public:
    using QWidget::QWidget;
    ~DocumentWorkbench() override = default;

    [[nodiscard]] virtual QString displayName() const = 0;
    [[nodiscard]] virtual bool dirty() const = 0;
    [[nodiscard]] virtual std::vector<std::filesystem::path> sourcePaths() const = 0;
    [[nodiscard]] virtual bool canSaveAs() const = 0;
    [[nodiscard]] virtual bool runSmokeChecks() = 0;
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
    [[nodiscard]] std::vector<std::filesystem::path> sourcePaths() const override;
    [[nodiscard]] bool canSaveAs() const override { return true; }
    [[nodiscard]] bool runSmokeChecks() override;
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
    [[nodiscard]] std::vector<std::filesystem::path> sourcePaths() const override;
    [[nodiscard]] bool canSaveAs() const override { return true; }
    [[nodiscard]] bool runSmokeChecks() override;
    void requestSaveAs(std::function<void(bool)> completed = {}) override;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

class PvrWorkbench final : public DocumentWorkbench {
public:
    PvrWorkbench(std::shared_ptr<spice::mix::PvrDocumentSession> session,
        RackTaskController& tasks, QWidget* parent = nullptr);
    ~PvrWorkbench() override;

    [[nodiscard]] QString displayName() const override;
    [[nodiscard]] bool dirty() const override;
    [[nodiscard]] std::vector<std::filesystem::path> sourcePaths() const override;
    [[nodiscard]] bool canSaveAs() const override { return true; }
    [[nodiscard]] bool runSmokeChecks() override;
    void requestSaveAs(std::function<void(bool)> completed = {}) override;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

class SstSmlWorkbench final : public DocumentWorkbench {
public:
    explicit SstSmlWorkbench(std::shared_ptr<spice::mix::SstSmlDocumentSession> session,
        QWidget* parent = nullptr);
    ~SstSmlWorkbench() override;

    [[nodiscard]] QString displayName() const override;
    [[nodiscard]] bool dirty() const override { return false; }
    [[nodiscard]] std::vector<std::filesystem::path> sourcePaths() const override;
    [[nodiscard]] bool canSaveAs() const override { return false; }
    [[nodiscard]] bool runSmokeChecks() override;
    void requestSaveAs(std::function<void(bool)> completed = {}) override;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

class EctWorkbench final : public DocumentWorkbench {
public:
    explicit EctWorkbench(std::shared_ptr<spice::mix::EctDocumentSession> session,
        QWidget* parent = nullptr);
    ~EctWorkbench() override;

    [[nodiscard]] QString displayName() const override;
    [[nodiscard]] bool dirty() const override { return false; }
    [[nodiscard]] std::vector<std::filesystem::path> sourcePaths() const override;
    [[nodiscard]] bool canSaveAs() const override { return false; }
    [[nodiscard]] bool runSmokeChecks() override;
    void requestSaveAs(std::function<void(bool)> completed = {}) override;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
