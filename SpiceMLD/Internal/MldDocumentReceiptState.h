#pragma once

#include "../Model/MldFile.h"

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
};

} // namespace spice::mld::detail
