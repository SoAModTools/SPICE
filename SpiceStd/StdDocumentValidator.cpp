#include "StdDocumentValidator.h"

#include "StdSha256.h"

#include <algorithm>
#include <limits>
#include <set>
#include <type_traits>

namespace spice::stdfile {
namespace {

void error(StdDocumentValidationResult& result, const StdDiagnosticCode code, std::string message) {
    result.diagnostics.push_back(StdDocumentDiagnostic{ code, StdDiagnosticSeverity::Error, std::move(message), std::nullopt });
}

template <typename Range>
bool validateIds(const Range& values, StdDocumentValidationResult& result, const char* name) {
    std::set<std::uint64_t> ids{};
    bool ok = true;
    for (const auto& value : values) {
        if (!value.id || !ids.insert(value.id.value).second) {
            error(result, StdDiagnosticCode::DuplicateId, std::string(name) + " IDs must be nonzero and unique.");
            ok = false;
            break;
        }
    }
    return ok;
}

std::size_t payloadSize(const StdEntryPayload& payload) {
    if (std::holds_alternative<StdActionViewPayload>(payload.content)) return kStdActionViewPayloadSize;
    return std::get<StdOpaquePayload>(payload.content).bytes.size();
}

bool contains(const std::vector<StdEntryPayloadId>& ids, const StdEntryPayloadId id) {
    return std::find(ids.begin(), ids.end(), id) != ids.end();
}

bool contains(const std::vector<StdOpaqueFragmentId>& ids, const StdOpaqueFragmentId id) {
    return std::find(ids.begin(), ids.end(), id) != ids.end();
}

} // namespace

spice::root::Endian byteOrderFor(const StdPlatform platform) noexcept {
    return platform == StdPlatform::Dreamcast ? spice::root::Endian::Little : spice::root::Endian::Big;
}

StdDocumentValidationResult StdDocumentValidator::validate(
    const StdDocument& document, const StdWriteTarget& target, const StdImportReceipt* receipt) {
    StdDocumentValidationResult result{};
    std::uint64_t decodedSize = 0U;

    if (const auto* rows = std::get_if<StdActionRowsContent>(&document.content)) {
        if (rows->rows.empty()) error(result, StdDiagnosticCode::InvalidDocument, "Action-row documents require at least one row.");
        validateIds(rows->rows, result, "Action row");
        decodedSize = 0x10ULL + static_cast<std::uint64_t>(rows->rows.size()) * 0x18ULL;
    } else if (const auto* table = std::get_if<StdEntryTableContent>(&document.content)) {
        if (table->kind != 4U) error(result, StdDiagnosticCode::InvalidDocument, "Entry-table kind must remain 4.");
        if (!table->terminator.id) error(result, StdDiagnosticCode::DuplicateId, "Entry-table terminator ID must be nonzero.");
        if (table->terminator.negativeLocation >= 0) error(result, StdDiagnosticCode::InvalidDocument, "Entry-table terminator location must be negative.");
        if (table->records.size() + 1U > std::numeric_limits<std::uint16_t>::max()) {
            error(result, StdDiagnosticCode::OutputTooLarge, "Entry-table record count exceeds the encoded U16 limit.");
        }
        validateIds(table->records, result, "Entry record");
        validateIds(table->payloads, result, "Entry payload");
        validateIds(table->opaqueFragments, result, "Opaque fragment");

        std::set<std::uint64_t> recordPayloadIds{};
        for (const auto& record : table->records) {
            if (!record.payload.has_value()) continue;
            const auto* payload = findEntryPayload(*table, *record.payload);
            if (payload == nullptr) {
                error(result, StdDiagnosticCode::DanglingReference, "Entry record references a missing payload.");
                continue;
            }
            if (!recordPayloadIds.insert(record.payload->value).second) {
                error(result, StdDiagnosticCode::DuplicateLayoutOwnership, "Each entry payload must be owned by exactly one record.");
            }
            if (std::holds_alternative<StdActionViewPayload>(payload->content) &&
                record.combinedType() != kStdActionViewCombinedType) {
                error(result, StdDiagnosticCode::InvalidDocument, "Typed action-view payload requires combined type 0x0003002A.");
            }
        }
        for (const auto& payload : table->payloads) {
            if (!recordPayloadIds.contains(payload.id.value)) {
                error(result, StdDiagnosticCode::MissingLayoutOwnership, "Entry payload is not owned by an entry record.");
            }
            if (payloadSize(payload) > std::numeric_limits<std::uint32_t>::max()) {
                error(result, StdDiagnosticCode::OutputTooLarge, "Entry payload exceeds the encoded U32 size limit.");
            }
        }

        std::set<std::pair<std::size_t, std::uint64_t>> layoutIds{};
        for (const auto& item : table->payloadLayout) {
            std::visit([&](const auto id) {
                using Id = std::remove_cv_t<decltype(id)>;
                const auto kind = std::is_same_v<Id, StdEntryPayloadId> ? 0U : 1U;
                if (!id || !layoutIds.insert({ kind, id.value }).second) {
                    error(result, StdDiagnosticCode::DuplicateLayoutOwnership, "Payload-area layout IDs must be nonzero and occur exactly once.");
                    return;
                }
                if constexpr (std::is_same_v<Id, StdEntryPayloadId>) {
                    if (findEntryPayload(*table, id) == nullptr) error(result, StdDiagnosticCode::DanglingReference, "Payload layout references a missing payload.");
                } else {
                    const auto* fragment = findOpaqueFragment(*table, id);
                    if (fragment == nullptr) error(result, StdDiagnosticCode::DanglingReference, "Payload layout references a missing opaque fragment.");
                    else if (fragment->bytes.empty()) error(result, StdDiagnosticCode::InvalidDocument, "Opaque payload-area fragments must not be empty.");
                }
            }, item);
        }
        for (const auto& payload : table->payloads) {
            if (!layoutIds.contains({ 0U, payload.id.value })) error(result, StdDiagnosticCode::MissingLayoutOwnership, "Entry payload is missing from payload-area layout.");
        }
        for (const auto& fragment : table->opaqueFragments) {
            if (!layoutIds.contains({ 1U, fragment.id.value })) error(result, StdDiagnosticCode::MissingLayoutOwnership, "Opaque fragment is missing from payload-area layout.");
        }

        decodedSize = 0x10ULL + static_cast<std::uint64_t>(table->records.size() + 1U) * 0x10ULL;
        for (const auto& item : table->payloadLayout) {
            std::visit([&](const auto id) {
                using Id = std::remove_cv_t<decltype(id)>;
                if constexpr (std::is_same_v<Id, StdEntryPayloadId>) {
                    if (const auto* payload = findEntryPayload(*table, id)) decodedSize += payloadSize(*payload);
                } else {
                    if (const auto* fragment = findOpaqueFragment(*table, id)) decodedSize += fragment->bytes.size();
                }
            }, item);
        }
    } else {
        const auto& opaque = std::get<StdOpaqueContent>(document.content);
        if (opaque.decodedBytes.empty()) error(result, StdDiagnosticCode::InvalidDocument, "Top-level opaque STD content must not be empty.");
        decodedSize = opaque.decodedBytes.size();
    }

    if (decodedSize > static_cast<std::uint64_t>(std::numeric_limits<std::uint32_t>::max()) + 0x10ULL) {
        error(result, StdDiagnosticCode::OutputTooLarge, "Decoded STD output exceeds the encoded U32 span limit.");
    }

    if (document.hasOpaqueContent()) {
        if (receipt == nullptr) {
            error(result, StdDiagnosticCode::ReceiptRequired, "Writing opaque STD content requires its import receipt.");
        } else if (receipt->byteOrder != byteOrderFor(target.platform)) {
            error(result, StdDiagnosticCode::OpaqueByteOrderMismatch, "Opaque STD content cannot be written with a different byte order.");
        } else if (const auto* opaque = std::get_if<StdOpaqueContent>(&document.content)) {
            if (!receipt->opaqueEvidence.topLevelDecodedSha256.has_value() ||
                detail::sha256(opaque->decodedBytes) != *receipt->opaqueEvidence.topLevelDecodedSha256) {
                error(result, StdDiagnosticCode::ReceiptMismatch, "Top-level opaque STD bytes do not match the import receipt.");
            }
        } else if (const auto* table = std::get_if<StdEntryTableContent>(&document.content)) {
            for (const auto& payload : table->payloads) {
                if (std::holds_alternative<StdOpaquePayload>(payload.content) &&
                    !contains(receipt->opaqueEvidence.payloadIds, payload.id)) {
                    error(result, StdDiagnosticCode::ReceiptMismatch, "Opaque entry payload is not covered by the import receipt.");
                }
            }
            for (const auto& fragment : table->opaqueFragments) {
                if (!contains(receipt->opaqueEvidence.fragmentIds, fragment.id)) {
                    error(result, StdDiagnosticCode::ReceiptMismatch, "Opaque payload-area fragment is not covered by the import receipt.");
                }
            }
        }
    }

    if (result.diagnostics.empty()) result.readiness = StdWriteReadiness::Ready;
    return result;
}

const char* toString(const StdPlatform value) noexcept {
    switch (value) { case StdPlatform::Dreamcast: return "dreamcast"; case StdPlatform::GameCube: return "gamecube"; }
    return "unknown";
}

const char* toString(const StdWriteReadiness value) noexcept {
    switch (value) { case StdWriteReadiness::Invalid: return "invalid"; case StdWriteReadiness::Ready: return "ready"; }
    return "unknown";
}

} // namespace spice::stdfile
