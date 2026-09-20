#include "PatchInternals.h"
#include "../../Compression/Aklz.h"

#include <algorithm>
#include <limits>

namespace spice::mld::patching::detail {
namespace {
void error(MldPatchApplyResult& result, std::string message, const ByteWrite* write = nullptr) {
    model::MldDiagnostic diagnostic{.severity = model::MldDiagnostic::Severity::Error};
    diagnostic.message = write ? write->context + ": " + message : std::move(message);
    if (write && write->offset <= std::numeric_limits<std::uint32_t>::max())
        diagnostic.sourceOffset = static_cast<std::uint32_t>(write->offset);
    result.diagnostics.push_back(std::move(diagnostic));
}
}

MldPatchApplyResult applyWrites(std::span<std::uint8_t> bytes, std::span<const ByteWrite> writes) {
    MldPatchApplyResult result;
    std::vector<const ByteWrite*> ordered;
    for (const auto& write : writes) ordered.push_back(&write);
    std::sort(ordered.begin(), ordered.end(), [](const auto* a, const auto* b) { return a->offset < b->offset; });
    std::size_t previousEnd = 0;
    for (const auto* write : ordered) {
        if (write->expected.empty() || write->expected.size() != write->replacement.size()
            || write->offset > bytes.size() || write->expected.size() > bytes.size() - write->offset) {
            error(result, "Invalid or out-of-bounds byte write.", write);
            continue;
        }
        if (write->offset < previousEnd) error(result, "Byte writes overlap.", write);
        previousEnd = std::max(previousEnd, write->offset + write->expected.size());
        if (!std::equal(write->expected.begin(), write->expected.end(), bytes.begin() + write->offset))
            error(result, "Source bytes do not match the expected value.", write);
    }
    if (!result.ok()) return result;
    for (const auto* write : ordered)
        std::copy(write->replacement.begin(), write->replacement.end(), bytes.begin() + write->offset);
    result.appliedPatchCount = writes.size();
    return result;
}

MldPatchApplyResult materializeWrites(std::span<const std::uint8_t> source,
    bool compressedAklz, std::span<const ByteWrite> writes) {
    MldPatchApplyResult result;
    if (compression::aklz::isAklz(source) != compressedAklz) {
        error(result, "Source compression does not match the patch plan.");
        return result;
    }
    if (writes.empty()) {
        result.bytes.assign(source.begin(), source.end());
        return result;
    }
    std::vector<std::uint8_t> decoded;
    if (compressedAklz) {
        auto unwrapped = compression::aklz::decompress(source);
        if (!unwrapped.ok()) {
            error(result, "AKLZ source decompression failed.");
            return result;
        }
        decoded = std::move(unwrapped.bytes);
    } else decoded.assign(source.begin(), source.end());
    auto applied = applyWrites(decoded, writes);
    if (!applied.ok()) return applied;
    if (compressedAklz) {
        auto encoded = compression::aklz::compress(decoded);
        if (!encoded.ok()) {
            error(result, "AKLZ patch compression failed.");
            return result;
        }
        result.bytes = std::move(encoded.bytes);
    } else result.bytes = std::move(decoded);
    result.appliedPatchCount = applied.appliedPatchCount;
    return result;
}
} // namespace spice::mld::patching::detail
