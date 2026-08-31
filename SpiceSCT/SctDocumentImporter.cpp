#include "SctDocumentImporter.h"

#include <algorithm>
#include <limits>
#include <numeric>
#include <unordered_map>

namespace spice::sct {
namespace {

void addDiagnostic(SctDocumentImportResult& result, SctDiagnosticSeverity severity,
    SctDiagnosticCode code, std::string message,
    std::optional<SctDocumentEntityId> entity = std::nullopt) {
    result.diagnostics.push_back({severity, code, std::move(entity), std::move(message)});
}

bool hasUnknownNode(const SctScptAstNode& node) {
    if (node.kind == SctScptAstNodeKind::Unknown) return true;
    return std::any_of(node.children.begin(), node.children.end(), hasUnknownNode);
}

SctCanonicalExpressionNodeKind canonicalKind(SctScptAstNodeKind kind) {
    switch (kind) {
    case SctScptAstNodeKind::NoLoopValue: return SctCanonicalExpressionNodeKind::NoLoopValue;
    case SctScptAstNodeKind::RawValue: return SctCanonicalExpressionNodeKind::RawValue;
    case SctScptAstNodeKind::FloatLiteral: return SctCanonicalExpressionNodeKind::FloatLiteral;
    case SctScptAstNodeKind::DecimalLiteral: return SctCanonicalExpressionNodeKind::DecimalLiteral;
    case SctScptAstNodeKind::IntVariable: return SctCanonicalExpressionNodeKind::IntVariable;
    case SctScptAstNodeKind::FloatVariable: return SctCanonicalExpressionNodeKind::FloatVariable;
    case SctScptAstNodeKind::BitVariable: return SctCanonicalExpressionNodeKind::BitVariable;
    case SctScptAstNodeKind::ByteVariable: return SctCanonicalExpressionNodeKind::ByteVariable;
    case SctScptAstNodeKind::SecondaryValue: return SctCanonicalExpressionNodeKind::SecondaryValue;
    case SctScptAstNodeKind::CompareOp: return SctCanonicalExpressionNodeKind::CompareOperator;
    case SctScptAstNodeKind::ArithmeticOp: return SctCanonicalExpressionNodeKind::ArithmeticOperator;
    case SctScptAstNodeKind::AssignmentOp: return SctCanonicalExpressionNodeKind::AssignmentOperator;
    case SctScptAstNodeKind::Stop: return SctCanonicalExpressionNodeKind::Stop;
    case SctScptAstNodeKind::Unknown: break;
    }
    return SctCanonicalExpressionNodeKind::RawValue;
}

SctCanonicalExpressionNode convertNode(const SctScptAstNode& node) {
    SctCanonicalExpressionNode converted;
    converted.kind = canonicalKind(node.kind);
    if (!node.rawWords.empty()) {
        converted.encodingCode = node.rawWords.front();
        converted.payloadWords.assign(node.rawWords.begin() + 1, node.rawWords.end());
    }
    converted.children.reserve(node.children.size());
    for (const auto& child : node.children) converted.children.push_back(convertNode(child));
    return converted;
}

SctCanonicalExpression convertExpression(const SctExpression& expression,
    const std::vector<std::uint32_t>& rawWords, SctDocumentImportResult& result,
    SctDocumentEntityId entity) {
    SctCanonicalExpression converted;
    converted.termination = expression.hitStopCode ? SctExpressionTermination::StopCode
                                                    : SctExpressionTermination::InlineValue;
    if (!expression.ast || hasUnknownNode(*expression.ast)) {
        converted.root = SctOpaqueExpression{rawWords};
        addDiagnostic(result, SctDiagnosticSeverity::Warning, SctDiagnosticCode::AmbiguousExpression,
            "SCPT expression retained as opaque words because its exact typed structure is incomplete.", entity);
    } else {
        converted.root = convertNode(*expression.ast);
    }
    return converted;
}

std::optional<SctEditableText> editableText(const std::vector<std::uint8_t>& bytes) {
    if (bytes.empty() || bytes.back() != 0) return std::nullopt;
    if (std::find(bytes.begin(), bytes.end() - 1, 0) != bytes.end() - 1) return std::nullopt;
    return SctEditableText{std::string(bytes.begin(), bytes.end() - 1)};
}

struct ClaimLedger {
    std::vector<std::uint8_t> claims;
    bool claim(std::size_t begin, std::size_t size) {
        if (begin > claims.size() || size > claims.size() - begin) return false;
        for (std::size_t i = begin; i < begin + size; ++i) if (claims[i]) return false;
        std::fill(claims.begin() + begin, claims.begin() + begin + size, 1);
        return true;
    }
};

using InstructionMap = std::unordered_map<std::uint32_t, SctInstructionId>;
using FooterMap = std::unordered_map<std::uint32_t, SctFooterEntryId>;

std::vector<std::size_t> physicalInstructionOrder(const SctSection& section) {
    std::vector<std::size_t> order(section.instructions.size());
    std::iota(order.begin(), order.end(), 0);
    std::stable_sort(order.begin(), order.end(), [&](std::size_t left, std::size_t right) {
        return section.instructions[left].offset < section.instructions[right].offset;
    });
    return order;
}

std::optional<SctInstructionId> edgeTarget(const SctSection& section, const SctInstruction& instruction,
    std::uint32_t parameterIndex, const SctOpcodeSchema& schema, const InstructionMap& ids) {
    SctEdgeType wanted = SctEdgeType::Jump;
    switch (schema.semantic.controlRole) {
    case SctOpcodeControlRole::Branch: wanted = SctEdgeType::BranchFalse; break;
    case SctOpcodeControlRole::Switch: wanted = SctEdgeType::SwitchCase; break;
    case SctOpcodeControlRole::Jump: wanted = SctEdgeType::Jump; break;
    case SctOpcodeControlRole::CallSubscript: wanted = SctEdgeType::CallSubscript; break;
    default: return std::nullopt;
    }
    std::vector<const SctEdge*> matching;
    for (const auto& edge : section.edges) {
        if (edge.type == wanted && edge.fromPayloadOffset == instruction.payloadOffset && edge.toPayloadOffset) {
            matching.push_back(&edge);
        }
    }
    const SctEdge* selected = nullptr;
    if (wanted == SctEdgeType::SwitchCase) {
        const auto repeated = sctOpcodeRepeatedGroup(schema);
        if (!repeated || parameterIndex < repeated->firstParameter) return std::nullopt;
        const auto width = repeated->lastParameter - repeated->firstParameter + 1;
        const auto ordinal = (parameterIndex - repeated->firstParameter) / width;
        if (ordinal < matching.size()) selected = matching[ordinal];
    } else if (!matching.empty()) {
        selected = matching.front();
    }
    if (!selected) return std::nullopt;
    const auto found = ids.find(*selected->toPayloadOffset);
    return found == ids.end() ? std::nullopt : std::optional(found->second);
}

std::optional<SctFooterEntryId> footerTarget(const SctInstruction& instruction,
    std::uint32_t parameterIndex, const SctFooter& footer, const FooterMap& ids) {
    for (const auto& entry : footer.entries) {
        for (const auto& reference : entry.references) {
            if (reference.instructionPayloadOffset == instruction.payloadOffset
                && reference.parameterIndex == parameterIndex) {
                const auto found = ids.find(entry.payloadOffset);
                if (found != ids.end()) return found->second;
            }
        }
    }
    return std::nullopt;
}

SctDocumentParameter makeParameter(const SctParameter& parameter, const SctInstruction& instruction,
    const SctSection& section, const SctOpcodeSchema& schema, const InstructionMap& instructionIds,
    const SctFooter* footer, const FooterMap& footerIds, SctDocumentImportResult& result,
    SctInstructionId entityId) {
    SctDocumentParameter converted;
    converted.schemaIndex = sctOpcodeBaseParameterIndex(schema, parameter.index);
    if (schema.semantic.controlRole != SctOpcodeControlRole::None) {
        const auto pattern = schema.parameters;
        const bool isTarget = static_cast<int>(converted.schemaIndex) == pattern.jumpParam
            || static_cast<int>(converted.schemaIndex) == pattern.switchJumpParam
            || (schema.semantic.controlRole == SctOpcodeControlRole::CallSubscript && converted.schemaIndex == 0);
        if (isTarget) {
            if (const auto target = edgeTarget(section, instruction, parameter.index, schema, instructionIds)) {
                converted.value = SctInstructionReference{*target};
                return converted;
            }
            converted.value = SctOpaqueParameterValue{parameter.rawWords};
            addDiagnostic(result, SctDiagnosticSeverity::Warning, SctDiagnosticCode::UnresolvedReference,
                "Control-flow target could not be resolved to an instruction ID.", SctDocumentEntityId{entityId});
            return converted;
        }
    }
    if (sctOpcodeFooterReference(schema, parameter.index).kind != SctFooterParamKind::None) {
        if (footer) {
            if (const auto target = footerTarget(instruction, parameter.index, *footer, footerIds)) {
                converted.value = SctFooterEntryReference{*target};
                return converted;
            }
        }
        converted.value = SctOpaqueParameterValue{parameter.rawWords};
        addDiagnostic(result, SctDiagnosticSeverity::Warning, SctDiagnosticCode::UnresolvedReference,
            "Footer target could not be resolved to a footer-entry ID.", SctDocumentEntityId{entityId});
        return converted;
    }
    if (sctOpcodeParameterEncoding(schema, parameter.index) == SctOpcodeParameterEncoding::ScptExpression) {
        if (parameter.expression) {
            converted.value = convertExpression(*parameter.expression, parameter.rawWords, result, SctDocumentEntityId{entityId});
        } else {
            converted.value = SctCanonicalExpression{SctOpaqueExpression{parameter.rawWords},
                SctExpressionTermination::InlineValue};
            addDiagnostic(result, SctDiagnosticSeverity::Warning, SctDiagnosticCode::AmbiguousExpression,
                "SCPT parameter had no parser AST and was retained as opaque words.", SctDocumentEntityId{entityId});
        }
    } else if (parameter.rawWords.size() == 1) {
        converted.value = SctEncodedWordValue{parameter.rawWords.front()};
    } else {
        converted.value = SctOpaqueParameterValue{parameter.rawWords};
    }
    return converted;
}

SctOpaqueAttachmentId addAttachment(SctDocument& document, SctDocumentImportResult& result, SctOpaqueAnchor anchor,
    std::uint32_t absoluteOffset, std::vector<std::uint8_t> bytes, SctOpaqueReason reason,
    SctDocumentSection* section = nullptr) {
    if (bytes.empty()) return {};
    const auto id = document.allocateOpaqueAttachmentId();
    document.opaqueAttachments.push_back({id, std::move(bytes), std::move(anchor),
        SctOpaquePlacement::FixedOffset, absoluteOffset, 1, SctOpaqueRelocationSupport::FixedOnly, reason});
    if (section) section->opaqueAttachments.push_back(id);
    result.receipt.provenance.push_back({SctDocumentEntityId{id}, absoluteOffset,
        static_cast<std::uint32_t>(document.opaqueAttachments.back().bytes.size()), std::nullopt});
    return id;
}

} // namespace

SctDocumentImportResult SctDocumentImporter::import(
    const SctParseResult& parsed,
    const SctDocumentImportOptions& options) {
    SctDocumentImportResult result;
    result.receipt.source.byteOrder = parsed.file.detectedEndian == "big" ? SctSourceByteOrder::BigEndian
        : parsed.file.detectedEndian == "little" ? SctSourceByteOrder::LittleEndian : SctSourceByteOrder::Unknown;
    result.receipt.source.wrapper = parsed.file.originalCompressedAklz ? SctSourceWrapper::Aklz : SctSourceWrapper::None;
    result.receipt.declaredSourcePlatform = options.declaredSourcePlatform;
    if (!parsed.parseOk) {
        addDiagnostic(result, SctDiagnosticSeverity::Error, SctDiagnosticCode::ParseFailed,
            "A canonical document cannot be imported from a failed parse.");
        return result;
    }
    const auto& bytes = parsed.file.originalPayloadBytes;
    const std::uint64_t dataStart64 = 12ull + (20ull * parsed.file.sections.size());
    if (bytes.size() < dataStart64) {
        addDiagnostic(result, SctDiagnosticSeverity::Error, SctDiagnosticCode::UnsafePhysicalStructure,
            "Decoded SCT payload is shorter than its physical index table.");
        return result;
    }
    for (const auto& section : parsed.file.sections) {
        if (section.startOffset > section.endOffset || section.endOffset > bytes.size()) {
            addDiagnostic(result, SctDiagnosticSeverity::Error, SctDiagnosticCode::UnsafePhysicalStructure,
                "A physical section has contradictory decoded-payload bounds.");
            return result;
        }
    }

    SctDocument document;
    std::vector<SctSectionId> sectionIds;
    sectionIds.reserve(parsed.file.sections.size());
    for (std::size_t i = 0; i < parsed.file.sections.size(); ++i) sectionIds.push_back(document.allocateSectionId());

    InstructionMap instructionIds;
    for (const auto& section : parsed.file.sections) {
        std::vector<int> owners(section.endOffset - section.startOffset, -1);
        std::vector<bool> unsafe(section.instructions.size(), false);
        const auto order = physicalInstructionOrder(section);
        for (const auto i : order) {
            const auto& instruction = section.instructions[i];
            if (!instruction.decodeOk || !findSctOpcodeSchema(instruction.opcode)
                || instruction.offset > owners.size() || instruction.sizeBytes > owners.size() - instruction.offset) {
                unsafe[i] = true;
                continue;
            }
            for (std::size_t pos = instruction.offset; pos < instruction.offset + instruction.sizeBytes; ++pos) {
                if (owners[pos] >= 0) {
                    unsafe[i] = true;
                    unsafe[static_cast<std::size_t>(owners[pos])] = true;
                } else {
                    owners[pos] = static_cast<int>(i);
                }
            }
        }
        for (const auto i : order) {
            if (!unsafe[i]) instructionIds.emplace(section.instructions[i].payloadOffset, document.allocateInstructionId());
        }
    }
    FooterMap footerIds;
    std::vector<std::size_t> footerEntryOrder;
    if (parsed.file.footer) {
        const auto& sourceFooter = *parsed.file.footer;
        footerEntryOrder.resize(sourceFooter.entries.size());
        std::iota(footerEntryOrder.begin(), footerEntryOrder.end(), 0);
        std::stable_sort(footerEntryOrder.begin(), footerEntryOrder.end(), [&](std::size_t left, std::size_t right) {
            return sourceFooter.entries[left].payloadOffset < sourceFooter.entries[right].payloadOffset;
        });
        const auto footerSize = sourceFooter.payloadEndOffset >= sourceFooter.payloadStartOffset
            ? sourceFooter.payloadEndOffset - sourceFooter.payloadStartOffset : 0;
        std::vector<int> owners(footerSize, -1);
        std::vector<bool> unsafe(sourceFooter.entries.size(), false);
        for (const auto i : footerEntryOrder) {
            const auto& entry = sourceFooter.entries[i];
            if (entry.payloadOffset < sourceFooter.payloadStartOffset) { unsafe[i] = true; continue; }
            const auto local = entry.payloadOffset - sourceFooter.payloadStartOffset;
            if (local > owners.size() || entry.rawBytes.size() > owners.size() - local) { unsafe[i] = true; continue; }
            for (std::size_t pos = local; pos < local + entry.rawBytes.size(); ++pos) {
                if (owners[pos] >= 0) {
                    unsafe[i] = true;
                    unsafe[static_cast<std::size_t>(owners[pos])] = true;
                } else owners[pos] = static_cast<int>(i);
            }
        }
        for (const auto i : footerEntryOrder) {
            if (!unsafe[i]) footerIds.emplace(sourceFooter.entries[i].payloadOffset, document.allocateFooterEntryId());
        }
    }

    result.receipt.provenance.push_back({{}, 8, 4, std::nullopt});
    std::vector<std::string> sectionNames;
    std::vector<SctOpaqueAttachmentId> indexPaddingAttachments(parsed.file.sections.size());
    sectionNames.reserve(parsed.file.sections.size());
    for (std::size_t sectionIndex = 0; sectionIndex < parsed.file.sections.size(); ++sectionIndex) {
        const auto rowOffset = static_cast<std::uint32_t>(12 + sectionIndex * 20);
        result.receipt.provenance.push_back({SctDocumentEntityId{sectionIds[sectionIndex]}, rowOffset,
            4, static_cast<std::uint32_t>(sectionIndex)});
        const auto nameBegin = bytes.begin() + rowOffset + 4;
        const auto zero = std::find(nameBegin, nameBegin + 16, 0);
        sectionNames.emplace_back(nameBegin, zero);
        if (!sectionNames.back().empty()) {
            result.receipt.provenance.push_back({SctDocumentEntityId{sectionIds[sectionIndex]}, rowOffset + 4,
                static_cast<std::uint32_t>(sectionNames.back().size()), static_cast<std::uint32_t>(sectionIndex)});
        }
        indexPaddingAttachments[sectionIndex] = addAttachment(document, result, sectionIds[sectionIndex],
            rowOffset + 4 + static_cast<std::uint32_t>(sectionNames.back().size()),
            std::vector<std::uint8_t>(zero, nameBegin + 16), SctOpaqueReason::Padding);
    }
    addAttachment(document, result, SctDocumentAnchor{}, 0,
        std::vector<std::uint8_t>(bytes.begin(), bytes.begin() + 8), SctOpaqueReason::Header);

    const SctFooter* footer = parsed.file.footer ? &*parsed.file.footer : nullptr;
    if (footer) {
        const auto dataSize = bytes.size() - static_cast<std::size_t>(dataStart64);
        if (footer->payloadStartOffset > footer->payloadEndOffset || footer->payloadEndOffset > dataSize) {
            addDiagnostic(result, SctDiagnosticSeverity::Error, SctDiagnosticCode::UnsafePhysicalStructure,
                "The parsed footer has contradictory decoded-payload bounds.");
            return result;
        }
    }
    for (std::size_t sectionIndex = 0; sectionIndex < parsed.file.sections.size(); ++sectionIndex) {
        const auto& sourceSection = parsed.file.sections[sectionIndex];
        SctDocumentSection targetSection;
        targetSection.id = sectionIds[sectionIndex];
        targetSection.nameBytes = sectionNames[sectionIndex];
        if (indexPaddingAttachments[sectionIndex]) {
            targetSection.opaqueAttachments.push_back(indexPaddingAttachments[sectionIndex]);
        }
        ClaimLedger ledger{std::vector<std::uint8_t>(sourceSection.endOffset - sourceSection.startOffset)};

        if (sourceSection.stringEntry) {
            const auto stringId = document.allocateStringId();
            const auto& entry = *sourceSection.stringEntry;
            SctDocumentString string{stringId, SctOpaqueText{entry.rawTextBytes}};
            if (const auto text = editableText(entry.rawTextBytes)) string.value = *text;
            else addDiagnostic(result, SctDiagnosticSeverity::Warning, SctDiagnosticCode::AmbiguousString,
                "String bytes were retained as opaque because single-byte termination was ambiguous.", SctDocumentEntityId{stringId});
            const auto local = static_cast<std::size_t>(entry.textStartOffset);
            if (!ledger.claim(local, entry.rawTextBytes.size())) {
                addDiagnostic(result, SctDiagnosticSeverity::Warning, SctDiagnosticCode::OverlappingSourceClaims,
                    "String evidence overlapped or exceeded its physical section and was preserved opaquely.", SctDocumentEntityId{stringId});
                targetSection.content = SctOpaqueSectionContent{};
            } else {
                document.strings.push_back(std::move(string));
                targetSection.content = SctStringSectionContent{stringId};
                result.receipt.provenance.push_back({SctDocumentEntityId{stringId},
                    sourceSection.startOffset + entry.textStartOffset,
                    static_cast<std::uint32_t>(entry.rawTextBytes.size()), static_cast<std::uint32_t>(sectionIndex)});
            }
        } else if (sourceSection.kind == SctSectionKind::Label) {
            targetSection.content = SctLabelSectionContent{};
        } else if (!sourceSection.instructions.empty() || sourceSection.kind == SctSectionKind::Script) {
            SctScriptSectionContent script;
            const auto instructionOrder = physicalInstructionOrder(sourceSection);
            for (const auto instructionIndex : instructionOrder) {
                const auto& instruction = sourceSection.instructions[instructionIndex];
                const auto idIt = instructionIds.find(instruction.payloadOffset);
                const auto* schema = findSctOpcodeSchema(instruction.opcode);
                const auto localOffset = static_cast<std::size_t>(instruction.offset);
                if (idIt == instructionIds.end() || !schema || !ledger.claim(localOffset, instruction.sizeBytes)) {
                    addDiagnostic(result, SctDiagnosticSeverity::Warning, SctDiagnosticCode::OverlappingSourceClaims,
                        "Instruction evidence could not be promoted safely; its bytes remain opaque.", SctDocumentEntityId{targetSection.id});
                    continue;
                }
                SctDocumentInstruction converted;
                converted.id = idIt->second;
                converted.opcode = instruction.opcode;
                converted.skipRefresh = instruction.skipRefresh;
                if (instruction.scheduled.present) {
                    const auto& scheduled = instruction.scheduled.frameDelay;
                    if (scheduled.expression) converted.scheduledExpression = convertExpression(
                        *scheduled.expression, scheduled.rawWords, result, SctDocumentEntityId{converted.id});
                    else converted.scheduledExpression = SctCanonicalExpression{SctOpaqueExpression{scheduled.rawWords}, SctExpressionTermination::InlineValue};
                }
                const auto repeated = sctOpcodeRepeatedGroup(*schema);
                std::uint32_t countValue = 0;
                bool countKnown = false;
                for (const auto& parameter : instruction.parameters) {
                    const auto base = sctOpcodeBaseParameterIndex(*schema, parameter.index);
                    if (repeated && base == repeated->iterationCountParameter && parameter.index < schema->parameters.paramCount) {
                        if (parameter.rawWords.size() == 1) { countValue = parameter.rawWords.front(); countKnown = true; }
                        continue;
                    }
                    auto canonical = makeParameter(parameter, instruction, sourceSection, *schema,
                        instructionIds, footer, footerIds, result, converted.id);
                    if (repeated && parameter.index >= repeated->firstParameter) {
                        const auto width = repeated->lastParameter - repeated->firstParameter + 1;
                        const auto ordinal = (parameter.index - repeated->firstParameter) / width;
                        if (converted.repeatedParameterGroups.size() <= ordinal) converted.repeatedParameterGroups.resize(ordinal + 1);
                        converted.repeatedParameterGroups[ordinal].parameters.push_back(std::move(canonical));
                    } else {
                        converted.fixedParameters.push_back(std::move(canonical));
                    }
                }
                if (repeated && countKnown && countValue != converted.repeatedParameterGroups.size()) {
                    addDiagnostic(result, SctDiagnosticSeverity::Warning, SctDiagnosticCode::RepeatedCountMismatch,
                        "Encoded repetition count disagrees with the imported group count.", SctDocumentEntityId{converted.id});
                }
                script.instructions.push_back(std::move(converted));
                result.receipt.provenance.push_back({SctDocumentEntityId{idIt->second},
                    sourceSection.startOffset + instruction.offset, instruction.sizeBytes, static_cast<std::uint32_t>(sectionIndex)});
            }
            targetSection.content = std::move(script);
        } else {
            targetSection.content = SctOpaqueSectionContent{};
        }

        for (std::size_t pos = 0; pos < ledger.claims.size();) {
            if (ledger.claims[pos]) { ++pos; continue; }
            const auto begin = pos;
            while (pos < ledger.claims.size() && !ledger.claims[pos]) ++pos;
            addAttachment(document, result, targetSection.id,
                sourceSection.startOffset + static_cast<std::uint32_t>(begin),
                std::vector<std::uint8_t>(bytes.begin() + sourceSection.startOffset + begin,
                    bytes.begin() + sourceSection.startOffset + pos),
                SctOpaqueReason::Gap, &targetSection);
        }
        document.sections.push_back(std::move(targetSection));
    }

    if (footer) {
        ClaimLedger ledger{std::vector<std::uint8_t>(footer->payloadEndOffset - footer->payloadStartOffset)};
        const auto dataStart = static_cast<std::uint32_t>(dataStart64);
        for (const auto entryIndex : footerEntryOrder) {
            const auto& entry = footer->entries[entryIndex];
            const auto idIt = footerIds.find(entry.payloadOffset);
            if (idIt == footerIds.end()) {
                addDiagnostic(result, SctDiagnosticSeverity::Warning, SctDiagnosticCode::OverlappingSourceClaims,
                    "Footer entry evidence could not be promoted safely and remains opaque.");
                continue;
            }
            const auto id = idIt->second;
            const auto kind = entry.kind == SctFooterEntryKind::SctString
                ? SctDocumentFooterEntryKind::SctString
                : SctDocumentFooterEntryKind::String;
            SctDocumentFooterEntry converted{id, kind, SctOpaqueText{entry.rawBytes}};
            if (const auto text = editableText(entry.rawBytes)) converted.value = *text;
            else addDiagnostic(result, SctDiagnosticSeverity::Warning, SctDiagnosticCode::AmbiguousString,
                "Footer bytes were retained as opaque because single-byte termination was ambiguous.", SctDocumentEntityId{id});
            const auto local = entry.payloadOffset - footer->payloadStartOffset;
            if (ledger.claim(local, entry.rawBytes.size())) {
                document.footerEntries.push_back(std::move(converted));
                result.receipt.provenance.push_back({SctDocumentEntityId{id}, dataStart + entry.payloadOffset,
                    static_cast<std::uint32_t>(entry.rawBytes.size()), std::nullopt});
            } else {
                addDiagnostic(result, SctDiagnosticSeverity::Warning, SctDiagnosticCode::OverlappingSourceClaims,
                    "Footer entry evidence overlapped or exceeded the footer and remains opaque.", SctDocumentEntityId{id});
            }
        }
        const auto footerAbsolute = dataStart + footer->payloadStartOffset;
        for (std::size_t pos = 0; pos < ledger.claims.size();) {
            if (ledger.claims[pos]) { ++pos; continue; }
            const auto begin = pos;
            while (pos < ledger.claims.size() && !ledger.claims[pos]) ++pos;
            addAttachment(document, result, SctDocumentAnchor{}, footerAbsolute + static_cast<std::uint32_t>(begin),
                std::vector<std::uint8_t>(bytes.begin() + footerAbsolute + begin, bytes.begin() + footerAbsolute + pos),
                SctOpaqueReason::Gap);
        }
    }

    std::vector<std::uint8_t> totalCoverage(bytes.size(), 0);
    for (const auto& provenance : result.receipt.provenance) {
        const auto begin = static_cast<std::size_t>(provenance.decodedPayloadOffset);
        const auto size = static_cast<std::size_t>(provenance.byteSize);
        if (begin > totalCoverage.size() || size > totalCoverage.size() - begin) {
            addDiagnostic(result, SctDiagnosticSeverity::Error, SctDiagnosticCode::UnsafePhysicalStructure,
                "An imported source claim exceeds the decoded SCT payload.");
            return result;
        }
        for (std::size_t pos = begin; pos < begin + size; ++pos) {
            if (totalCoverage[pos] != 0) {
                addDiagnostic(result, SctDiagnosticSeverity::Error, SctDiagnosticCode::UnsafePhysicalStructure,
                    "Contradictory physical bounds prevent lossless decoded-payload coverage.");
                return result;
            }
            totalCoverage[pos] = 1;
        }
    }
    for (std::size_t pos = 0; pos < totalCoverage.size();) {
        if (totalCoverage[pos]) { ++pos; continue; }
        const auto begin = pos;
        while (pos < totalCoverage.size() && !totalCoverage[pos]) ++pos;
        addAttachment(document, result, SctDocumentAnchor{}, static_cast<std::uint32_t>(begin),
            std::vector<std::uint8_t>(bytes.begin() + begin, bytes.begin() + pos), SctOpaqueReason::Gap);
    }

    result.document = std::move(document);
    return result;
}

} // namespace spice::sct
