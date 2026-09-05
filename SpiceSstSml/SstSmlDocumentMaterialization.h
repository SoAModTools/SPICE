#pragma once

#include "SstSmlDocumentImporter.h"
#include "../SpiceMLD/MldDocumentWriter.h"

#include <optional>
#include <vector>

namespace spice::sstsml {

struct SstSmlMaterializationResult {
    std::vector<std::uint8_t> bytes{};
    std::vector<SstSmlDocumentDiagnostic> diagnostics{};
    [[nodiscard]] bool ok() const noexcept;
};

[[nodiscard]] SstSmlMaterializationResult materializeCommandPayload(
    const SstStageCommand& command,
    spice::root::Endian endian);

[[nodiscard]] SstSmlMaterializationResult materializeEmbeddedResource(
    const SmlEmbeddedResource& resource,
    const SstSmlDocumentImportReceipt& receipt,
    std::optional<spice::mld::MldWriteTarget> fallbackTarget = std::nullopt);

} // namespace spice::sstsml
