#include "Application/OperationRunner.h"
#include "Cli/CliParser.h"

#include <filesystem>
#include <iostream>

int main(int argc, char** argv) {
    const auto parsed = spice::fileparsing::cli::parse(argc, argv);
    if (parsed.disposition == spice::fileparsing::cli::ParseDisposition::Help) {
        std::cout << parsed.text;
        return 0;
    }
    if (parsed.disposition == spice::fileparsing::cli::ParseDisposition::Error) {
        std::cerr << "SpiceFileParsing: " << parsed.text << "\n"
                  << "Use 'SpiceFileParsing --help' for usage.\n";
        return 2;
    }

    spice::fileparsing::OperationContext context{};
    const auto processPath = std::filesystem::absolute(std::filesystem::path(argv[0]));
    context.executableDirectory = processPath.parent_path();
    context.report = [](const spice::fileparsing::OperationEvent& event) {
        auto& stream = event.level == spice::fileparsing::EventLevel::Warning
            || event.level == spice::fileparsing::EventLevel::Error
            ? std::cerr
            : std::cout;
        stream << event.message << '\n';
    };

    const auto result = spice::fileparsing::OperationRunner{}.run(*parsed.request, context);
    switch (result.status) {
    case spice::fileparsing::OperationStatus::Success:
        return 0;
    case spice::fileparsing::OperationStatus::Cancelled:
        return 1;
    case spice::fileparsing::OperationStatus::Failure:
        return 1;
    }
    return 1;
}
