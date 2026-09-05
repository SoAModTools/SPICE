#include "StdDocumentValidator.h"

#include "StdSha256.h"

#include <algorithm>
#include <limits>
#include <set>
#include <type_traits>

namespace spice::stdfile {
namespace {

void diagnose(StdDocumentValidationResult& result, const StdDiagnosticCode code,
    const StdDiagnosticSeverity severity, std::string message) {
    result.diagnostics.push_back(StdDocumentDiagnostic{ code, severity, std::move(message), std::nullopt });
}

void error(StdDocumentValidationResult& result, const StdDiagnosticCode code, std::string message) {
    diagnose(result, code, StdDiagnosticSeverity::Error, std::move(message));
}

void warning(StdDocumentValidationResult& result, const StdDiagnosticCode code, std::string message) {
    diagnose(result, code, StdDiagnosticSeverity::Warning, std::move(message));
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
    return std::visit([](const auto& value) -> std::size_t {
        using Payload = std::decay_t<decltype(value)>;
        if constexpr (std::is_same_v<Payload, StdSparcPayload>) return kStdSparcPayloadSize;
        else if constexpr (std::is_same_v<Payload, StdPutModelPayload>) return kStdPutModelPayloadSize;
        else if constexpr (std::is_same_v<Payload, StdSetCommandPayload>) return kStdSetCommandPayloadSize;
        else if constexpr (std::is_same_v<Payload, StdMotionPausePayload>) return kStdMotionPausePayloadSize;
        else if constexpr (std::is_same_v<Payload, StdCollisionBoxPayload>) return kStdCollisionBoxPayloadSize;
        else if constexpr (std::is_same_v<Payload, StdMoveModelPayload>) return kStdMoveModelPayloadSize;
        else if constexpr (std::is_same_v<Payload, StdHitWeaponPayload>) return kStdHitWeaponPayloadSize;
        else if constexpr (std::is_same_v<Payload, StdPointLightPayload>) return kStdPointLightPayloadSize;
        else if constexpr (std::is_same_v<Payload, StdSystemCameraPayload>) return kStdSystemCameraPayloadSize;
        else if constexpr (std::is_same_v<Payload, StdEffectWaitPayload>) return kStdEffectWaitPayloadSize;
        else if constexpr (std::is_same_v<Payload, StdSeRequestPayload>) return kStdSeRequestPayloadSize;
        else return value.bytes.size();
    }, payload.content);
}

std::optional<std::uint32_t> typedCombinedType(const StdEntryPayload& payload) {
    return std::visit([](const auto& value) -> std::optional<std::uint32_t> {
        using Payload = std::decay_t<decltype(value)>;
        if constexpr (std::is_same_v<Payload, StdSparcPayload>) return kStdSparcCombinedType;
        else if constexpr (std::is_same_v<Payload, StdPutModelPayload>) return kStdPutModelCombinedType;
        else if constexpr (std::is_same_v<Payload, StdSetCommandPayload>) return kStdSetCommandCombinedType;
        else if constexpr (std::is_same_v<Payload, StdMotionPausePayload>) return kStdMotionPauseCombinedType;
        else if constexpr (std::is_same_v<Payload, StdCollisionBoxPayload>) return kStdCollisionBoxCombinedType;
        else if constexpr (std::is_same_v<Payload, StdMoveModelPayload>) return kStdMoveModelCombinedType;
        else if constexpr (std::is_same_v<Payload, StdHitWeaponPayload>) return kStdHitWeaponCombinedType;
        else if constexpr (std::is_same_v<Payload, StdPointLightPayload>) return kStdPointLightCombinedType;
        else if constexpr (std::is_same_v<Payload, StdSystemCameraPayload>) return kStdSystemCameraCombinedType;
        else if constexpr (std::is_same_v<Payload, StdEffectWaitPayload>) return kStdEffectWaitCombinedType;
        else if constexpr (std::is_same_v<Payload, StdSeRequestPayload>) return kStdSeRequestCombinedType;
        else return std::nullopt;
    }, payload.content);
}

void validateTimelineRepeatRange(
    const std::int16_t first, const std::int16_t last, StdDocumentValidationResult& result) {
    if (first >= last) return;
    constexpr auto kTimelineSize = static_cast<std::int16_t>(64);
    if (first < 0 || last < 0 || first >= kTimelineSize || last >= kTimelineSize) {
        error(result, StdDiagnosticCode::InvalidTimelineRepeatRange,
            "An enabled model timeline repeat range must remain within the 64 serialized entries.");
    }
}

void validatePayloadSemantics(
    const StdEntryPayload& payload, StdDocumentValidationResult& result) {
    std::visit([&](const auto& value) {
        using Payload = std::decay_t<decltype(value)>;
        if constexpr (std::is_same_v<Payload, StdPutModelPayload> ||
            std::is_same_v<Payload, StdMoveModelPayload> ||
            std::is_same_v<Payload, StdHitWeaponPayload>) {
            validateTimelineRepeatRange(value.timelineRepeatFirst, value.timelineRepeatLast, result);
        } else if constexpr (std::is_same_v<Payload, StdPointLightPayload>) {
            if (value.lightSlot > 3) {
                error(result, StdDiagnosticCode::UnsupportedPointLightSlot,
                    "POINT LIGHT slots greater than 3 are rejected by the game reader.");
            }
        }
    }, payload.content);
}

template <typename Id>
bool contains(const std::vector<Id>& ids, const Id id) {
    return std::find(ids.begin(), ids.end(), id) != ids.end();
}

bool hasErrors(const StdDocumentValidationResult& result) {
    return std::any_of(result.diagnostics.begin(), result.diagnostics.end(), [](const auto& item) {
        return item.severity == StdDiagnosticSeverity::Error;
    });
}

} // namespace

spice::root::Endian byteOrderFor(const StdPlatform platform) noexcept {
    return platform == StdPlatform::Dreamcast ? spice::root::Endian::Little : spice::root::Endian::Big;
}

StdDocumentValidationResult StdDocumentValidator::validate(
    const StdDocument& document, const StdWriteTarget& target, const StdImportReceipt* receipt) {
    StdDocumentValidationResult result{};
    std::uint64_t declaredSize = 0U;

    if (const auto* rows = std::get_if<StdActionRowsContent>(&document.content)) {
        if (rows->rows.empty()) error(result, StdDiagnosticCode::InvalidDocument, "Action-row documents require at least one row.");
        validateIds(rows->rows, result, "Action row");
        for (const auto& row : rows->rows) {
            if (const auto* fields = std::get_if<StdUnrecognizedActionRowFields>(&row.fields)) {
                if (fields->rowType == 0 || fields->rowType == 1 || fields->rowType == 3) {
                    error(result, StdDiagnosticCode::InvalidDocument,
                        fields->rowType == 3
                            ? "Action-row type 3 is a loader-generated runtime terminator and cannot be serialized."
                            : "Established row types 0 and 1 must use their dedicated action-row variants.");
                    continue;
                }
                warning(result, StdDiagnosticCode::UnrecognizedActionRowType,
                    "Action row uses a source row type whose semantics are not established.");
            }
        }
        declaredSize = 0x10ULL + static_cast<std::uint64_t>(rows->rows.size()) * 0x18ULL;
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
        if (table->fileTrailer.has_value()) {
            if (!table->fileTrailer->id || std::any_of(table->opaqueFragments.begin(), table->opaqueFragments.end(),
                [&](const auto& value) { return value.id == table->fileTrailer->id; })) {
                error(result, StdDiagnosticCode::DuplicateId,
                    "File-trailer ID must be nonzero and unique among opaque fragment IDs.");
            }
            if (table->fileTrailer->bytes.empty()) {
                error(result, StdDiagnosticCode::InvalidDocument, "A present file trailer must not be empty.");
            }
        }

        std::set<std::uint64_t> recordPayloadIds{};
        for (const auto& record : table->records) {
            const auto* descriptor = findStdCommandDescriptor(record.combinedType());
            if (!record.payload.has_value()) {
                if (descriptor != nullptr) {
                    error(result, StdDiagnosticCode::KnownPayloadMissing,
                        "Known STD command requires the fixed-size payload materialized by the game loader.");
                }
                continue;
            }
            const auto* payload = findEntryPayload(*table, *record.payload);
            if (payload == nullptr) {
                error(result, StdDiagnosticCode::DanglingReference, "Entry record references a missing payload.");
                continue;
            }
            if (!recordPayloadIds.insert(record.payload->value).second) {
                error(result, StdDiagnosticCode::DuplicateLayoutOwnership, "Each entry payload must be owned by exactly one record.");
            }
            const auto typedType = typedCombinedType(*payload);
            if (typedType.has_value() && *typedType != record.combinedType()) {
                error(result, StdDiagnosticCode::InvalidDocument,
                    "Typed payload variant does not match its owning record's combined type.");
            }
            if (descriptor != nullptr && payloadSize(*payload) != descriptor->loaderPayloadSize) {
                error(result, StdDiagnosticCode::KnownPayloadSizeMismatch,
                    "Known STD command payload does not match its fixed loader size.");
            }
            if (descriptor != nullptr && descriptor->hasTypedPayloadCodec && !typedType.has_value()) {
                error(result, StdDiagnosticCode::InvalidDocument,
                    "Command with a complete SPICE codec requires its typed payload variant.");
            }
            if (descriptor != nullptr && !descriptor->hasTypedPayloadCodec && typedType.has_value()) {
                error(result, StdDiagnosticCode::InvalidDocument,
                    "Command without a complete SPICE codec must retain its opaque payload representation.");
            }
        }
        for (const auto& payload : table->payloads) {
            if (!recordPayloadIds.contains(payload.id.value)) {
                error(result, StdDiagnosticCode::MissingLayoutOwnership, "Entry payload is not owned by an entry record.");
            }
            validatePayloadSemantics(payload, result);
            if (payloadSize(payload) > std::numeric_limits<std::uint32_t>::max()) {
                error(result, StdDiagnosticCode::OutputTooLarge, "Entry payload exceeds the encoded U32 size limit.");
            }
        }

        std::set<std::pair<std::size_t, std::uint64_t>> layoutIds{};
        std::uint64_t cursor = 0x10ULL + static_cast<std::uint64_t>(table->records.size() + 1U) * 0x10ULL;
        for (const auto& item : table->payloadLayout) {
            std::visit([&](const auto id) {
                using Id = std::remove_cv_t<decltype(id)>;
                const auto kind = std::is_same_v<Id, StdEntryPayloadId> ? 0U : 1U;
                if (!id || !layoutIds.insert({ kind, id.value }).second) {
                    error(result, StdDiagnosticCode::DuplicateLayoutOwnership,
                        "Payload-area layout IDs must be nonzero and occur exactly once.");
                    return;
                }
                if constexpr (std::is_same_v<Id, StdEntryPayloadId>) {
                    const auto* payload = findEntryPayload(*table, id);
                    if (payload == nullptr) {
                        error(result, StdDiagnosticCode::DanglingReference, "Payload layout references a missing payload.");
                        return;
                    }
                    const auto owner = std::find_if(table->records.begin(), table->records.end(),
                        [&](const auto& record) { return record.payload.has_value() && *record.payload == id; });
                    const auto* descriptor = owner == table->records.end()
                        ? nullptr : findStdCommandDescriptor(owner->combinedType());
                    if (descriptor != nullptr && cursor % 4ULL != 0ULL) {
                        warning(result, StdDiagnosticCode::KnownPayloadMisaligned,
                            "Known fixed-size payload is not four-byte aligned.");
                    }
                    cursor += payloadSize(*payload);
                } else {
                    const auto* fragment = findOpaqueFragment(*table, id);
                    if (fragment == nullptr) {
                        error(result, StdDiagnosticCode::DanglingReference,
                            "Payload layout references a missing opaque fragment.");
                    } else if (fragment->bytes.empty()) {
                        error(result, StdDiagnosticCode::InvalidDocument,
                            "Opaque payload-area fragments must not be empty.");
                    } else {
                        cursor += fragment->bytes.size();
                    }
                }
            }, item);
        }
        for (const auto& payload : table->payloads) {
            if (!layoutIds.contains({ 0U, payload.id.value })) {
                error(result, StdDiagnosticCode::MissingLayoutOwnership,
                    "Entry payload is missing from payload-area layout.");
            }
        }
        for (const auto& fragment : table->opaqueFragments) {
            if (!layoutIds.contains({ 1U, fragment.id.value })) {
                error(result, StdDiagnosticCode::MissingLayoutOwnership,
                    "Opaque fragment is missing from payload-area layout.");
            }
        }
        declaredSize = cursor;

    } else {
        const auto& opaque = std::get<StdOpaqueContent>(document.content);
        if (opaque.decodedBytes.empty()) error(result, StdDiagnosticCode::InvalidDocument, "Top-level opaque STD content must not be empty.");
        declaredSize = opaque.decodedBytes.size();
    }

    if (declaredSize > static_cast<std::uint64_t>(std::numeric_limits<std::uint32_t>::max()) + 0x10ULL) {
        error(result, StdDiagnosticCode::OutputTooLarge, "Decoded STD declared span exceeds the encoded U32 limit.");
    }

    if (document.hasOpaqueContent()) {
        if (receipt == nullptr) {
            error(result, StdDiagnosticCode::ReceiptRequired, "Writing opaque STD content requires its import receipt.");
        } else if (receipt->byteOrder != byteOrderFor(target.platform)) {
            error(result, StdDiagnosticCode::OpaqueByteOrderMismatch,
                "Opaque STD content cannot be written with a different byte order.");
        } else if (const auto* opaque = std::get_if<StdOpaqueContent>(&document.content)) {
            if (!receipt->opaqueEvidence.topLevelDecodedSha256.has_value() ||
                detail::sha256(opaque->decodedBytes) != *receipt->opaqueEvidence.topLevelDecodedSha256) {
                error(result, StdDiagnosticCode::ReceiptMismatch,
                    "Top-level opaque STD bytes do not match the import receipt.");
            }
        } else if (const auto* table = std::get_if<StdEntryTableContent>(&document.content)) {
            for (const auto& payload : table->payloads) {
                if (std::holds_alternative<StdOpaquePayload>(payload.content) &&
                    !contains(receipt->opaqueEvidence.payloadIds, payload.id)) {
                    error(result, StdDiagnosticCode::ReceiptMismatch,
                        "Opaque entry payload is not covered by the import receipt.");
                }
            }
            for (const auto& fragment : table->opaqueFragments) {
                if (!contains(receipt->opaqueEvidence.fragmentIds, fragment.id)) {
                    error(result, StdDiagnosticCode::ReceiptMismatch,
                        "Opaque payload-area fragment is not covered by the import receipt.");
                }
            }
            if (table->fileTrailer.has_value() &&
                (!receipt->opaqueEvidence.fileTrailerId.has_value() ||
                    *receipt->opaqueEvidence.fileTrailerId != table->fileTrailer->id ||
                    !receipt->opaqueEvidence.fileTrailerSha256.has_value() ||
                    detail::sha256(table->fileTrailer->bytes) != *receipt->opaqueEvidence.fileTrailerSha256)) {
                error(result, StdDiagnosticCode::ReceiptMismatch,
                    "File trailer does not exactly match its import receipt.");
            }
        }
    }

    if (!hasErrors(result)) result.readiness = StdWriteReadiness::Ready;
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
