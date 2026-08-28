#include "OperationRunner.h"

#include "OperationPreflight.h"
#include "../Operations/OperationExecution.h"

#include <exception>
#include <string>
#include <string_view>

namespace spice::mix {
OperationResult OperationRunner::run(const OperationRequest& request, OperationContext& context) const {
    if (context.stopToken.stop_requested()) {
        return { OperationStatus::Cancelled, 0 };
    }
    if (const auto validationError = preflight::validate(request)) {
        if (context.report) {
            context.report({ EventLevel::Error, *validationError });
        }
        return { OperationStatus::Failure, 0 };
    }

    std::size_t filesProcessed = 0;
    const auto originalReporter = context.report;
    context.report = [&](const OperationEvent& event) {
        constexpr std::string_view prefix = "FilesProcessed=";
        if (event.message.starts_with(prefix)) {
            try {
                filesProcessed = static_cast<std::size_t>(std::stoull(event.message.substr(prefix.size())));
            } catch (const std::exception&) {
            }
        }
        if (originalReporter) {
            originalReporter(event);
        }
    };

    int exitCode = 1;
    try {
        exitCode = detail::executeOperation(request, context);
        if (exitCode != 0 && context.report) {
            context.report({ EventLevel::Error, "operation failed" });
        }
    } catch (const std::exception& ex) {
        if (context.report) {
            context.report({ EventLevel::Error, ex.what() });
        }
    }
    context.report = originalReporter;
    if (context.stopToken.stop_requested()) {
        return { OperationStatus::Cancelled, filesProcessed };
    }
    return { exitCode == 0 ? OperationStatus::Success : OperationStatus::Failure, filesProcessed };
}

} // namespace spice::mix
