#pragma once

#include "SctDocument.h"
#include "SctDocumentImporter.h"
#include "SctOpcodeMetadata.h"

#include <array>
#include <memory>
#include <optional>
#include <span>
#include <vector>

namespace spice::sct {

namespace detail {
struct SctValidationReceiptState;
}

// Immutable proof that neutral validation succeeded for one exact typed
// document state. Consumers may retain and copy the receipt. A downstream
// operation reuses it only when its deterministic fingerprint still matches;
// mutation therefore causes safe revalidation rather than stale acceptance.
class SctDocumentValidationReceipt final {
public:
    SctDocumentValidationReceipt(const SctDocumentValidationReceipt&) noexcept = default;
    SctDocumentValidationReceipt& operator=(const SctDocumentValidationReceipt&) noexcept = default;
    [[nodiscard]] std::span<const std::uint8_t, 32> validationFingerprint() const noexcept;
    [[nodiscard]] std::span<const SctDocumentDiagnostic> diagnostics() const noexcept;

private:
    friend class SctDocumentValidator;
    friend class SctDocumentLayoutEngine;
    friend class SctDocumentExporter;
    explicit SctDocumentValidationReceipt(
        std::shared_ptr<const detail::SctValidationReceiptState> state) noexcept
        : state_(std::move(state)) {}
    std::shared_ptr<const detail::SctValidationReceiptState> state_;
};

// Immutable proof that target validation succeeded for one exact document,
// platform, text encoding, and optional bound-evidence lineage.
class SctTargetValidationReceipt final {
public:
    SctTargetValidationReceipt(const SctTargetValidationReceipt&) noexcept = default;
    SctTargetValidationReceipt& operator=(const SctTargetValidationReceipt&) noexcept = default;
    [[nodiscard]] std::span<const std::uint8_t, 32> validationFingerprint() const noexcept;
    [[nodiscard]] std::span<const SctDocumentDiagnostic> diagnostics() const noexcept;

private:
    friend class SctDocumentValidator;
    friend class SctDocumentLayoutEngine;
    friend class SctDocumentExporter;
    explicit SctTargetValidationReceipt(
        std::shared_ptr<const detail::SctValidationReceiptState> state) noexcept
        : state_(std::move(state)) {}
    std::shared_ptr<const detail::SctValidationReceiptState> state_;
};

enum class SctValidationReceiptUse {
    NotProvided,
    Reused,
    MismatchRevalidated,
};

struct SctDocumentValidationResult {
    bool validDocument = false;
    std::vector<SctDocumentDiagnostic> diagnostics;
    std::optional<SctDocumentValidationReceipt> receipt;
};

struct SctTargetValidationResult {
    bool validForTarget = false;
    std::vector<SctDocumentDiagnostic> diagnostics;
    std::vector<SctOpaqueAttachmentId> unresolvedOpaqueAttachments;
    std::optional<SctTargetValidationReceipt> receipt;
    SctValidationReceiptUse neutralReceiptUse = SctValidationReceiptUse::NotProvided;
};

class SctDocumentValidator {
public:
    [[nodiscard]] static SctDocumentValidationResult validateDocument(
        const SctDocument& document);
    [[nodiscard]] static SctTargetValidationResult validateForTarget(
        const SctDocument& document,
        SctPlatform targetPlatform,
        SctTextEncoding textEncoding,
        const SctBoundImportEvidence* evidence = nullptr,
        const SctDocumentValidationReceipt* documentReceipt = nullptr);
};

} // namespace spice::sct
