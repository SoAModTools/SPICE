#pragma once

#include "StdDocumentImporter.h"
#include "StdJsonExporter.h"
#include "StdSha256.h"

namespace spice::stdfile::detail {

struct StdProjectionEvidence {
    std::shared_ptr<const std::uint8_t> token{};
    std::array<std::uint8_t, 32U> documentHash{};
    std::array<std::uint8_t, 32U> sourceHash{};
    std::uint64_t sourceSize{}, decodedSize{};
    spice::root::Endian endian{};
    StdCompression compression{};
    StdByteOrderSelection byteOrderSelection{};
    std::vector<std::uint64_t> payloadOffsets{};
};

// Private in-process integrity value; not a persistent interchange fingerprint.
inline std::array<std::uint8_t, 32U> projectionDocumentHash(const StdDocument& document) {
    const auto text = StdJsonExporter{}.toJson(document);
    return sha256({reinterpret_cast<const std::uint8_t*>(text.data()), text.size()});
}

} // namespace spice::stdfile::detail
