#include "../SpiceMix/Application/OperationRunner.h"
#include "Cli/CliParser.h"

#include <filesystem>
#include <iostream>

int main(int argc, char** argv) {
    const auto parsed = spice::grinder::cli::parse(argc, argv);
    if (parsed.disposition == spice::grinder::cli::ParseDisposition::Help) {
        std::cout << parsed.text;
        return 0;
    }
    if (parsed.disposition == spice::grinder::cli::ParseDisposition::Error) {
        std::cerr << "SpiceGrinder: " << parsed.text << "\n"
                  << "Use 'SpiceGrinder --help' for usage.\n";
        return 2;
    }

    spice::mix::OperationContext context{};
    const auto processPath = std::filesystem::absolute(std::filesystem::path(argv[0]));
    context.executableDirectory = processPath.parent_path();
    context.report = [](const spice::mix::OperationEvent& event) {
        auto& stream = event.level == spice::mix::EventLevel::Warning
            || event.level == spice::mix::EventLevel::Error
            ? std::cerr
            : std::cout;
        stream << event.message << '\n';
    };

    const auto result = spice::mix::OperationRunner{}.run(*parsed.request, context);
    switch (result.status) {
    case spice::mix::OperationStatus::Success:
        return 0;
    case spice::mix::OperationStatus::Cancelled:
        return 1;
    case spice::mix::OperationStatus::Failure:
        return 1;
    }
    return 1;
}
