#pragma once

#include "SctDocument.h"
#include "SctHeaderContract.h"
#include "SctModel.h"
#include "SctOpcodeMetadata.h"
#include "SctSourceMap.h"
#include "SctStringGroups.h"

#include <array>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace spice::sct {

enum class SctSourceByteOrder { BigEndian, LittleEndian, Unknown };
enum class SctSourceWrapper { None, Aklz };
struct SctSourceObservations {
    SctSourceByteOrder byteOrder = SctSourceByteOrder::Unknown;
    SctSourceWrapper wrapper = SctSourceWrapper::None;
    struct Header {
        std::array<std::uint8_t, 8> rawBytes{};
        SctHeaderValues values;
        bool available = false;
    } header;
};

enum class SctFooterTextPromotionPolicy {
    PreserveAmbiguous,
    TrustSelectedEncoding,
};

struct SctDocumentImportOptions {
    std::optional<SctPlatform> declaredSourcePlatform;
    std::optional<SctTextEncoding> sourceTextEncoding;
    SctFooterTextPromotionPolicy footerTextPromotion =
        SctFooterTextPromotionPolicy::PreserveAmbiguous;
};

enum class SctTextImportDisposition {
    Semantic,
    OpaqueNoEncoding,
    OpaqueDecodeFailed,
    OpaqueConflictingInterpretations,
};

struct SctTextImportObservation {
    SctDocumentEntityId entity;
    std::optional<SctTextEncoding> selectedEncoding;
    SctTextImportDisposition disposition = SctTextImportDisposition::OpaqueNoEncoding;
    std::vector<SctKnownTextConvention> viableAlternativeConventions;
    std::string reason;
};

struct SctUnresolvedReferenceObservation {
    SctInstructionId sourceInstruction;
    SctParameterAddress parameter;
    std::uint32_t sourceInstructionPayloadOffset = 0;
    std::optional<std::uint32_t> operandPayloadOffset;
    std::optional<std::int64_t> calculatedTargetPayloadOffset;
};

struct SctImportLineageId {
    std::array<std::uint8_t, 32> sha256{};
    auto operator<=>(const SctImportLineageId&) const = default;
};

struct SctDocumentRevisionProvenance {
    SctImportLineageId importLineage;
    auto operator<=>(const SctDocumentRevisionProvenance&) const = default;
};

struct SctDocumentImportReceipt {
    SctImportLineageId lineage;
    SctSourceObservations source;
    std::optional<SctPlatform> declaredSourcePlatform;
    std::optional<SctTextEncoding> sourceTextEncoding;
    SctFooterTextPromotionPolicy footerTextPromotion =
        SctFooterTextPromotionPolicy::PreserveAmbiguous;
    SctImportedSourceMap sourceMap;
    std::vector<SctImportedControlFlowObservation> controlFlow;
    std::vector<SctImportedIndexedStringGroupObservation> indexedStringGroups;
    std::vector<SctTextImportObservation> text;
    std::vector<SctUnresolvedReferenceObservation> unresolvedReferences;
};

class SctBoundImportEvidence {
public:
    [[nodiscard]] const SctDocumentImportReceipt& receipt() const noexcept { return *receipt_; }
    [[nodiscard]] SctImportLineageId lineage() const noexcept { return receipt_->lineage; }

private:
    friend class SctDocumentImportContext;
    explicit SctBoundImportEvidence(
        std::shared_ptr<const SctDocumentImportReceipt> receipt) noexcept
        : receipt_(std::move(receipt)) {}
    std::shared_ptr<const SctDocumentImportReceipt> receipt_;
};

class SctDocumentImportContext {
public:
    SctDocumentImportContext();
    explicit SctDocumentImportContext(SctDocumentImportReceipt receipt);

    [[nodiscard]] const SctDocumentImportReceipt& receipt() const noexcept { return *receipt_; }
    [[nodiscard]] const SctDocumentRevisionProvenance& revisionProvenance() const noexcept {
        return revisionProvenance_;
    }

    [[nodiscard]] std::optional<SctBoundImportEvidence> bind(
        const SctDocumentRevisionProvenance& revision) const noexcept {
        if (revision.importLineage != receipt_->lineage) return std::nullopt;
        return SctBoundImportEvidence{receipt_};
    }

private:
    std::shared_ptr<const SctDocumentImportReceipt> receipt_;
    SctDocumentRevisionProvenance revisionProvenance_;
};

struct SctDocumentImportResult {
    std::optional<SctDocument> document;
    std::vector<SctDocumentDiagnostic> diagnostics;
    SctDocumentImportContext context;
};

class SctDocumentImporter {
public:
    [[nodiscard]] static SctDocumentImportResult import(
        const SctParseResult& parsed,
        const SctDocumentImportOptions& options = {});
};

} // namespace spice::sct
