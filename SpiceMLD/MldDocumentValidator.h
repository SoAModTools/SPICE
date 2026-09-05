#pragma once

#include "MldDocumentImporter.h"

#include <vector>

namespace spice::mld {

struct MldWriteTarget {
    MldPlatform platform{ MldPlatform::GameCube };
    MldWrapper wrapper{ MldWrapper::Aklz };
};

enum class MldWriteReadiness { Invalid, Ready };

struct MldDocumentValidationResult {
    MldWriteReadiness readiness{ MldWriteReadiness::Invalid };
    std::vector<MldDocumentDiagnostic> diagnostics{};
    [[nodiscard]] bool ok() const noexcept { return readiness == MldWriteReadiness::Ready; }
};

class MldDocumentValidator {
public:
    [[nodiscard]] static MldDocumentValidationResult validate(
        const MldDocument& document,
        const MldWriteTarget& target,
        const MldImportReceipt* receipt = nullptr);
};

} // namespace spice::mld
