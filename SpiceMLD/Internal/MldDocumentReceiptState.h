#pragma once

#include "../Model/MldFile.h"
#include "../MldDocumentImporter.h"

namespace spice::mld::detail {

struct MldPreservedFragment {
    std::size_t decodedOffset{ 0U };
    std::vector<std::uint8_t> bytes{};
};

struct MldImportState {
    // Structural encoding evidence only. Complete source and decoded byte images,
    // decoded resources, diagnostics, and derived analysis are deliberately not
    // retained by the receipt.
    model::MldFile encodingSkeleton{};
    std::vector<MldPreservedFragment> preservedFragments{};
    std::shared_ptr<const std::uint8_t> token{};
    std::array<std::uint8_t, 32U> sourceHash{};
    std::uint64_t sourceSize{}, decodedSize{};
    MldPlatform platform{};
    MldWrapper wrapper{};
    spice::root::Endian endian{};
    std::vector<MldEntryId> entryOrder{};
    std::vector<std::array<std::uint8_t, 32U>> entryHashes{};
};

[[nodiscard]] std::array<std::uint8_t, 32U> entryBindingHash(const MldEntry& entry);

} // namespace spice::mld::detail
