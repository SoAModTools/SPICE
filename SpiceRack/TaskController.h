#pragma once

#include "../SpiceMix/Documents/DocumentTypes.h"

#include <QtCore/QObject>

#include <functional>
#include <memory>
#include <stop_token>
#include <string>

class QThread;

class RackTaskController final : public QObject {
public:
    using Work = std::function<void(const spice::mix::DocumentContext&)>;

    explicit RackTaskController(QObject* parent = nullptr);
    ~RackTaskController() override;

    [[nodiscard]] bool busy() const noexcept;
    bool run(std::string label, Work work, std::function<void()> finished = {});
    void cancel();

    void setEventSink(std::function<void(const spice::mix::OperationEvent&)> sink);
    void setBusySink(std::function<void(bool, const std::string&)> sink);

private:
    QThread* thread_ = nullptr;
    std::shared_ptr<std::stop_source> stopSource_{};
    std::function<void(const spice::mix::OperationEvent&)> eventSink_{};
    std::function<void(bool, const std::string&)> busySink_{};
};
