#include "SctReferenceRepair.h"

#include "SctOpcodeMetadata.h"

#include <algorithm>
#include <string>

namespace spice::sct {
namespace {

void addError(SctReferenceValueResult& result, SctDiagnosticCode code, std::string message,
    SctInstructionId source, SctParameterAddress parameter) {
    SctDocumentDiagnostic diagnostic{SctDiagnosticSeverity::Error, code,
        std::move(message), SctDiagnosticLocation{SctParameterSite{source, parameter}}};
    result.diagnostics.push_back(std::move(diagnostic));
}

const SctDocumentParameter* findParameter(const SctDocumentInstruction& instruction,
    SctParameterAddress address) {
    const std::vector<SctDocumentParameter>* parameters = &instruction.fixedParameters;
    if (address.repeatedGroupOrdinal) {
        if (*address.repeatedGroupOrdinal >= instruction.repeatedParameterGroups.size()) return nullptr;
        parameters = &instruction.repeatedParameterGroups[*address.repeatedGroupOrdinal].parameters;
    }
    const auto found = std::find_if(parameters->begin(), parameters->end(), [&](const auto& parameter) {
        return parameter.schemaIndex == address.schemaIndex;
    });
    return found == parameters->end() ? nullptr : &*found;
}

std::optional<SctExpectedReferenceTarget> expectedTarget(
    const SctOpcodeSchema& schema, std::uint32_t schemaIndex) {
    const auto* parameter = sctOpcodeParameterSchema(schema, schemaIndex);
    if (parameter != nullptr && parameter->referenceKind == SctOpcodeReferenceKind::Instruction) {
        return SctExpectedReferenceTarget{SctReferenceTargetStorage::Instruction, std::nullopt};
    }
    if (const auto rule = sctOpcodeTextReference(schema, schemaIndex)) {
        return SctExpectedReferenceTarget{
            rule->storage == SctTextStorage::IndexedSection
                ? SctReferenceTargetStorage::IndexedString
                : SctReferenceTargetStorage::FooterEntry,
            rule->kind};
    }
    return std::nullopt;
}

bool addressMatches(const SctOpcodeSchema& schema, SctParameterAddress address) {
    const auto* parameter = sctOpcodeParameterSchema(schema, address.schemaIndex);
    return parameter != nullptr
        && (parameter->belongsToRepeatedGroup == address.repeatedGroupOrdinal.has_value());
}

bool targetMatches(const SctDocument& document, const SctDocumentIndex& index,
    const SctExpectedReferenceTarget& expected,
    const SctDocumentReferenceTarget& target) {
    if (expected.storage == SctReferenceTargetStorage::Instruction) {
        const auto* id = std::get_if<SctInstructionId>(&target);
        return id != nullptr && index.find(document, *id) != nullptr && !expected.textKind.has_value();
    }
    if (!expected.textKind.has_value()) return false;
    if (expected.storage == SctReferenceTargetStorage::IndexedString) {
        const auto* id = std::get_if<SctStringId>(&target);
        const auto* string = id == nullptr ? nullptr : index.find(document, *id);
        return string != nullptr && string->kind == *expected.textKind;
    }
    const auto* id = std::get_if<SctFooterEntryId>(&target);
    const auto* footer = id == nullptr ? nullptr : index.find(document, *id);
    return footer != nullptr && footer->kind == *expected.textKind;
}

SctDocumentParameterValue typedValue(const SctDocumentReferenceTarget& target) {
    return std::visit([](const auto& id) -> SctDocumentParameterValue {
        using T = std::decay_t<decltype(id)>;
        if constexpr (std::is_same_v<T, SctInstructionId>) return SctInstructionReference{id};
        else if constexpr (std::is_same_v<T, SctStringId>) return SctStringReference{id};
        else return SctFooterEntryReference{id};
    }, target);
}

void addCandidate(std::vector<SctReferenceRepairCandidate>& candidates,
    const SctDocumentReferenceTarget& target) {
    if (std::none_of(candidates.begin(), candidates.end(), [&](const auto& candidate) {
        return candidate.target == target;
    })) candidates.push_back({target});
}

} // namespace

SctReferenceRepairAnalysis SctReferenceRepair::analyze(
    const SctDocument& document, const SctBoundImportEvidence* evidence) {
    SctReferenceRepairAnalysis result;
    const auto index = SctDocumentIndex::build(document);
    for (const auto& section : document.sections) {
        const auto* script = std::get_if<SctScriptSectionContent>(&section.content);
        if (script == nullptr) continue;
        for (const auto& instruction : script->instructions) {
            const auto inspect = [&](const SctDocumentParameter& parameter,
                std::optional<std::uint32_t> groupOrdinal) {
                const auto* unresolved = std::get_if<SctUnresolvedReferenceValue>(&parameter.value);
                if (unresolved == nullptr) return;
                SctReferenceRepairIssue issue{instruction.id,
                    {parameter.schemaIndex, groupOrdinal}, unresolved->expectedTarget,
                    unresolved->encodedWords};
                if (evidence != nullptr) {
                    const auto& receipt = evidence->receipt();
                    const auto observation = std::find_if(receipt.unresolvedReferences.begin(),
                        receipt.unresolvedReferences.end(), [&](const auto& value) {
                            return value.sourceInstruction == instruction.id
                                && value.parameter == issue.parameter;
                        });
                    if (observation != receipt.unresolvedReferences.end()) {
                        issue.sourceObservation = *observation;
                        if (observation->calculatedTargetPayloadOffset
                            && *observation->calculatedTargetPayloadOffset >= 0) {
                            const auto offset = static_cast<std::uint64_t>(
                                *observation->calculatedTargetPayloadOffset);
                            for (const auto& provenance : receipt.sourceMap.recordsAt(
                                    static_cast<std::uint32_t>(offset))) {
                                if (provenance.span.offset != offset || !provenance.target) continue;
                                const auto* entity = std::get_if<SctDocumentEntityId>(&*provenance.target);
                                if (entity == nullptr) continue;
                                if (issue.expectedTarget.storage == SctReferenceTargetStorage::Instruction) {
                                    if (const auto* id = std::get_if<SctInstructionId>(entity);
                                        id != nullptr && index.find(document, *id) != nullptr) addCandidate(issue.candidates, *id);
                                } else if (issue.expectedTarget.storage == SctReferenceTargetStorage::FooterEntry) {
                                    if (const auto* id = std::get_if<SctFooterEntryId>(entity);
                                        id != nullptr && targetMatches(document, index, issue.expectedTarget, *id)) {
                                        addCandidate(issue.candidates, *id);
                                    }
                                } else if (const auto* sectionId = std::get_if<SctSectionId>(entity)) {
                                    const auto* targetSection = index.find(document, *sectionId);
                                    const auto* content = targetSection == nullptr ? nullptr
                                        : std::get_if<SctStringSectionContent>(&targetSection->content);
                                    if (content != nullptr && targetMatches(document, index, issue.expectedTarget,
                                            SctDocumentReferenceTarget{content->string.id})) {
                                        addCandidate(issue.candidates, content->string.id);
                                    }
                                }
                            }
                        }
                    }
                }
                result.issues.push_back(std::move(issue));
            };
            for (const auto& parameter : instruction.fixedParameters) inspect(parameter, std::nullopt);
            for (std::size_t ordinal = 0; ordinal < instruction.repeatedParameterGroups.size(); ++ordinal) {
                for (const auto& parameter : instruction.repeatedParameterGroups[ordinal].parameters) {
                    inspect(parameter, static_cast<std::uint32_t>(ordinal));
                }
            }
        }
    }
    return result;
}

SctReferenceValueResult SctReferenceRepair::createReferenceValue(
    const SctDocument& document, SctInstructionId sourceInstruction,
    SctParameterAddress parameter, const SctDocumentReferenceTarget& target) {
    SctReferenceValueResult result;
    const auto index = SctDocumentIndex::build(document);
    const auto* instruction = index.find(document, sourceInstruction);
    if (instruction == nullptr) {
        addError(result, SctDiagnosticCode::UnresolvedReference,
            "Source instruction does not exist in the document.", sourceInstruction, parameter);
        return result;
    }
    const auto* schema = findSctOpcodeSchema(instruction->opcode);
    if (schema == nullptr || !addressMatches(*schema, parameter)
        || findParameter(*instruction, parameter) == nullptr) {
        addError(result, SctDiagnosticCode::ParameterMismatch,
            "Parameter address does not identify an existing source parameter slot.",
            sourceInstruction, parameter);
        return result;
    }
    const auto expected = expectedTarget(*schema, parameter.schemaIndex);
    if (!expected) {
        addError(result, SctDiagnosticCode::ParameterMismatch,
            "Parameter address is not a known reference parameter.", sourceInstruction, parameter);
        return result;
    }
    if (!targetMatches(document, index, *expected, target)) {
        addError(result, SctDiagnosticCode::ParameterMismatch,
            "Selected target does not exist or does not match the reference contract.",
            sourceInstruction, parameter);
        return result;
    }
    result.value = typedValue(target);
    return result;
}

SctReferenceValueResult SctReferenceRepair::createReferenceValue(
    const SctDocument& document, std::uint16_t sourceOpcode,
    SctParameterAddress parameter, const SctDocumentReferenceTarget& target) {
    SctReferenceValueResult result;
    const auto* schema = findSctOpcodeSchema(sourceOpcode);
    if (schema == nullptr || !addressMatches(*schema, parameter)) {
        addError(result, SctDiagnosticCode::ParameterMismatch,
            "Parameter address is outside the source opcode shape.", {}, parameter);
        return result;
    }
    const auto expected = expectedTarget(*schema, parameter.schemaIndex);
    if (!expected) {
        addError(result, SctDiagnosticCode::ParameterMismatch,
            "Parameter address is not a known reference parameter.", {}, parameter);
        return result;
    }
    const auto index = SctDocumentIndex::build(document);
    if (!targetMatches(document, index, *expected, target)) {
        addError(result, SctDiagnosticCode::ParameterMismatch,
            "Selected target does not exist or does not match the reference contract.", {}, parameter);
        return result;
    }
    result.value = typedValue(target);
    return result;
}

SctReferenceValueResult SctReferenceRepair::resolve(
    const SctDocument& document, SctInstructionId sourceInstruction,
    SctParameterAddress parameter, const SctDocumentReferenceTarget& target) {
    SctReferenceValueResult result;
    const auto index = SctDocumentIndex::build(document);
    const auto* instruction = index.find(document, sourceInstruction);
    const auto* source = instruction == nullptr ? nullptr : findParameter(*instruction, parameter);
    const auto* unresolved = source == nullptr ? nullptr
        : std::get_if<SctUnresolvedReferenceValue>(&source->value);
    if (unresolved == nullptr) {
        addError(result, SctDiagnosticCode::ParameterMismatch,
            "Parameter is not an unresolved reference.", sourceInstruction, parameter);
        return result;
    }
    result = createReferenceValue(document, sourceInstruction, parameter, target);
    if (result.value) {
        const auto* schema = findSctOpcodeSchema(instruction->opcode);
        const auto expected = schema == nullptr ? std::nullopt : expectedTarget(*schema, parameter.schemaIndex);
        if (!expected || unresolved->expectedTarget != *expected) {
            result.value.reset();
            addError(result, SctDiagnosticCode::ParameterMismatch,
                "Unresolved reference classification disagrees with its source opcode schema.",
                sourceInstruction, parameter);
        }
    }
    return result;
}

} // namespace spice::sct
