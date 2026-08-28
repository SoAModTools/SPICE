#pragma once

#include "../Application/Operation.h"

#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace spice::fileparsing::cli {

enum class ParseDisposition {
    Run,
    Help,
    Error,
};

struct ParseResult {
    ParseDisposition disposition = ParseDisposition::Error;
    std::optional<OperationRequest> request{};
    std::string text{};
};

[[nodiscard]] ParseResult parse(std::span<const std::string_view> arguments);
[[nodiscard]] ParseResult parse(int argc, char** argv);
[[nodiscard]] std::string globalHelp();

} // namespace spice::fileparsing::cli
