#pragma once

#include "StdDocumentImporter.h"

namespace spice::stdfile {

enum class StdPlatform { Dreamcast, GameCube };

struct StdWriteTarget {
    StdPlatform platform{ StdPlatform::GameCube };
    StdCompression compression{ StdCompression::Aklz };
};

enum class StdWriteReadiness { Invalid, Ready };

struct StdDocumentValidationResult {
    StdWriteReadiness readiness{ StdWriteReadiness::Invalid };
    std::vector<StdDocumentDiagnostic> diagnostics{};
    [[nodiscard]] bool ok() const noexcept { return readiness == StdWriteReadiness::Ready; }
};

class StdDocumentValidator {
public:
    [[nodiscard]] static StdDocumentValidationResult validate(
        const StdDocument& document,
        const StdWriteTarget& target,
        const StdImportReceipt* receipt = nullptr);
};

[[nodiscard]] spice::root::Endian byteOrderFor(StdPlatform platform) noexcept;
[[nodiscard]] const char* toString(StdPlatform value) noexcept;
[[nodiscard]] const char* toString(StdWriteReadiness value) noexcept;

} // namespace spice::stdfile
