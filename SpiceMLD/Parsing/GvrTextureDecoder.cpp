#include "GvrTextureDecoder.h"

#include "../../SpiceGvm/SpiceGvm.h"

#include <utility>

namespace spice::mld::parsing {

DecodedRgbaTexture decodeGvrToRgba8(const model::MldTextureEntry& entry) {
    DecodedRgbaTexture out{};
    out.width = entry.width;
    out.height = entry.height;
    out.diagnostics.reserve(entry.diagnostics.size());
    for (const auto& diagnostic : entry.diagnostics)
        out.diagnostics.push_back(diagnostic.message);

    if (entry.decoded && !entry.rgba8.empty()) {
        out.decoded = true;
        out.rgba8 = entry.rgba8;
        return out;
    }

    auto fallback = spice::gvm::decoding::makeErrorTexture(entry.width, entry.height);
    out.decoded = true;
    out.width = fallback.width;
    out.height = fallback.height;
    out.rgba8 = std::move(fallback.rgba8);
    out.diagnostics.push_back("GVR decode wrapper emitted ERROR checkerboard fallback.");
    return out;
}

} // namespace spice::mld::parsing
