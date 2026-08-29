#include "TaskController.h"

#include <QtCore/QMetaObject>
#include <QtCore/QPointer>
#include <QtCore/QThread>

#include <exception>
#include <utility>

RackTaskController::RackTaskController(QObject* parent)
    : QObject(parent) {}

RackTaskController::~RackTaskController() {
    cancel();
    if (thread_) {
        thread_->quit();
        thread_->wait();
    }
}

bool RackTaskController::busy() const noexcept { return thread_ != nullptr; }

bool RackTaskController::run(std::string label, Work work, std::function<void()> finished) {
    if (busy()) return false;
    stopSource_ = std::make_shared<std::stop_source>();
    const auto stopSource = stopSource_;
    QPointer<RackTaskController> self(this);
    thread_ = QThread::create([self, stopSource, work = std::move(work)]() mutable {
        spice::mix::DocumentContext context{};
        context.stopToken = stopSource->get_token();
        context.report = [self](const spice::mix::OperationEvent& event) {
            if (!self) return;
            QMetaObject::invokeMethod(self, [self, event]() {
                if (self && self->eventSink_) self->eventSink_(event);
            }, Qt::QueuedConnection);
        };
        try {
            work(context);
        } catch (const std::exception& error) {
            context.report({ .level = spice::mix::EventLevel::Error, .message = error.what() });
        } catch (...) {
            context.report({ .level = spice::mix::EventLevel::Error, .message = "Unexpected background task failure." });
        }
    });
    connect(thread_, &QThread::finished, this,
        [this, thread = thread_, label, finished = std::move(finished)]() mutable {
            thread->deleteLater();
            thread_ = nullptr;
            stopSource_.reset();
            if (busySink_) busySink_(false, label);
            if (finished) finished();
        });
    if (busySink_) busySink_(true, label);
    thread_->start();
    return true;
}

void RackTaskController::cancel() {
    if (stopSource_) stopSource_->request_stop();
}

void RackTaskController::setEventSink(
    std::function<void(const spice::mix::OperationEvent&)> sink) {
    eventSink_ = std::move(sink);
}

void RackTaskController::setBusySink(
    std::function<void(bool, const std::string&)> sink) {
    busySink_ = std::move(sink);
}
