#pragma once

#include "StdModel.h"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace spice::stdfile {

struct StdWriteOptions {
    std::optional<StdSourceEncoding> sourceEncoding{};
    bool preserveExactSourceWhenUnchanged{ true };
};

struct StdWriteLayoutRecord {
    std::string kind{};
    std::uint32_t sourceOffset{ 0U };
    std::uint32_t outputOffset{ 0U };
    std::size_t sourceSize{ 0U };
    std::size_t outputSize{ 0U };
    bool copiedVerbatim{ false };
};

struct StdWriteResult {
    std::vector<std::uint8_t> bytes{};
    std::vector<StdDiagnostic> diagnostics{};
    std::vector<StdWriteLayoutRecord> layout{};
    std::size_t sourceSize{ 0U };
    std::size_t outputSize{ 0U };

    [[nodiscard]] bool ok() const noexcept;
};

class StdFileWriter {
public:
    [[nodiscard]] StdWriteResult write(const StdFile& file, const StdWriteOptions& options = {}) const;
};

} // namespace spice::stdfile
