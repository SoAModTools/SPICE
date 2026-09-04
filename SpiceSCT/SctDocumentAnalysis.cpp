#include "SctDocumentAnalysis.h"

#include "SctOpcodeMetadata.h"

#include <algorithm>
#include <functional>
#include <limits>
#include <type_traits>

namespace spice::sct {
namespace {

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

std::optional<SctInstructionId> instructionTarget(const SctDocumentInstruction& instruction,
    SctParameterAddress address) {
    const auto* parameter = findParameter(instruction, address);
    if (parameter == nullptr) return std::nullopt;
    if (const auto* reference = std::get_if<SctInstructionReference>(&parameter->value)) {
        return reference->target;
    }
    return std::nullopt;
}

void addParameterEdge(std::vector<SctControlFlowEdge>& edges,
    const SctDocumentInstruction& instruction, SctControlFlowKind kind,
    SctParameterAddress address, SctSemanticConfidence confidence) {
    edges.push_back({instruction.id, kind, confidence,
        SctParameterSite{instruction.id, address}, instructionTarget(instruction, address),
        std::nullopt, {}});
}

std::optional<SctControlFlowKind> unresolvedKind(const SctOpcodeSchema& schema,
    SctParameterAddress address) {
    switch (schema.semantic.controlRole) {
    case SctOpcodeControlRole::Branch:
        if (schema.parameters.jumpParam >= 0
            && address.schemaIndex == static_cast<std::uint32_t>(schema.parameters.jumpParam)) {
            return SctControlFlowKind::BranchFalse;
        }
        break;
    case SctOpcodeControlRole::Switch:
        if (schema.parameters.switchJumpParam >= 0
            && address.schemaIndex == static_cast<std::uint32_t>(schema.parameters.switchJumpParam)) {
            return SctControlFlowKind::SwitchCase;
        }
        break;
    case SctOpcodeControlRole::Jump: return SctControlFlowKind::Jump;
    case SctOpcodeControlRole::CallSubscript: return SctControlFlowKind::Call;
    default: break;
    }
    return std::nullopt;
}

struct ImportedOpaqueSpan {
    SctImportedByteSpan span;
    SctOpaqueAttachmentId id;
};

std::vector<SctOpaqueAttachmentId> crossedAttachments(const SctImportedSourceMap& sourceMap,
    std::span<const ImportedOpaqueSpan> opaqueSpans,
    const SctImportedControlFlowObservation& observation) {
    std::vector<SctOpaqueAttachmentId> result;
    const auto source = sourceMap.location(SctDocumentEntityId{observation.sourceInstruction});
    if (!source || !observation.targetPayloadOffset) return result;

    std::uint64_t low = 0;
    std::uint64_t high = 0;
    if (observation.targetInstruction) {
        const auto target = sourceMap.location(SctDocumentEntityId{*observation.targetInstruction});
        if (!target) return result;
        if (source->primarySpan.endOffset() <= target->primarySpan.offset) {
            low = source->primarySpan.endOffset();
            high = target->primarySpan.offset;
        } else if (target->primarySpan.endOffset() <= source->primarySpan.offset) {
            low = target->primarySpan.endOffset();
            high = source->primarySpan.offset;
        } else return result;
    } else if (*observation.targetPayloadOffset >= source->primarySpan.endOffset()) {
        low = source->primarySpan.endOffset();
        high = *observation.targetPayloadOffset;
    } else if (*observation.targetPayloadOffset < source->primarySpan.offset) {
        low = *observation.targetPayloadOffset;
        high = source->primarySpan.offset;
    } else return result;

    auto found = std::lower_bound(opaqueSpans.begin(), opaqueSpans.end(), low,
        [](const ImportedOpaqueSpan& candidate, std::uint64_t offset) {
            return candidate.span.endOffset() <= offset;
        });
    for (; found != opaqueSpans.end() && found->span.offset < high; ++found) {
        result.push_back(found->id);
    }
    return result;
}

std::optional<SctVariableKind> variableKind(SctScptValueKind kind) {
    switch (kind) {
    case SctScptValueKind::FloatBackedIntegerVariable:
    case SctScptValueKind::IntegerVariable:
    case SctScptValueKind::IntegerVariableLow16Comparison:
        return SctVariableKind::Integer;
    case SctScptValueKind::FloatVariable: return SctVariableKind::Float;
    case SctScptValueKind::BitVariable: return SctVariableKind::Bit;
    case SctScptValueKind::ByteVariable: return SctVariableKind::Byte;
    default: return std::nullopt;
    }
}

int confidenceRank(SctSemanticConfidence confidence) {
    switch (confidence) {
    case SctSemanticConfidence::Unknown: return 0;
    case SctSemanticConfidence::Heuristic: return 1;
    case SctSemanticConfidence::Partial: return 2;
    case SctSemanticConfidence::Known: return 3;
    }
    return 0;
}

SctOpcodeEffectUsability parameterUsability(const SctDocumentInstruction& instruction,
    const SctOpcodeSchema& schema, SctParameterAddress address) {
    const auto* value = findParameter(instruction, address);
    if (value == nullptr) return SctOpcodeEffectUsability::MissingInput;
    if (std::holds_alternative<SctUnresolvedReferenceValue>(value->value)) {
        return SctOpcodeEffectUsability::UnresolvedInput;
    }
    if (std::holds_alternative<SctOpaqueParameterValue>(value->value)) {
        return SctOpcodeEffectUsability::OpaqueInput;
    }
    if (const auto* expression = std::get_if<SctCanonicalExpression>(&value->value)) {
        return std::holds_alternative<SctOpaqueExpression>(expression->body)
            ? SctOpcodeEffectUsability::OpaqueInput : SctOpcodeEffectUsability::Usable;
    }
    const auto* contract = sctOpcodeParameterSchema(schema, address.schemaIndex);
    if (contract == nullptr) return SctOpcodeEffectUsability::IncompatibleInput;
    if (contract->referenceKind == SctOpcodeReferenceKind::Instruction) {
        return std::holds_alternative<SctInstructionReference>(value->value)
            ? SctOpcodeEffectUsability::Usable : SctOpcodeEffectUsability::IncompatibleInput;
    }
    if (contract->referenceKind == SctOpcodeReferenceKind::Text) {
        const bool compatible = contract->textReference
            && (contract->textReference->storage == SctTextStorage::IndexedSection
                ? std::holds_alternative<SctStringReference>(value->value)
                : std::holds_alternative<SctSupplementaryTextReference>(value->value));
        return compatible ? SctOpcodeEffectUsability::Usable
                          : SctOpcodeEffectUsability::IncompatibleInput;
    }
    if (contract->encoding == SctOpcodeParameterEncoding::RawWordsUntilSentinel) {
        return std::holds_alternative<SctTerminatedWordSequenceValue>(value->value)
            ? SctOpcodeEffectUsability::Usable : SctOpcodeEffectUsability::IncompatibleInput;
    }
    return std::holds_alternative<SctEncodedWordValue>(value->value)
        ? SctOpcodeEffectUsability::Usable : SctOpcodeEffectUsability::IncompatibleInput;
}

SctOpcodeEffectUsability combineUsability(SctOpcodeEffectUsability left,
    SctOpcodeEffectUsability right) {
    const auto rank = [](SctOpcodeEffectUsability value) {
        switch (value) {
        case SctOpcodeEffectUsability::MissingInput: return 4;
        case SctOpcodeEffectUsability::UnresolvedInput: return 3;
        case SctOpcodeEffectUsability::OpaqueInput: return 2;
        case SctOpcodeEffectUsability::IncompatibleInput: return 1;
        case SctOpcodeEffectUsability::Usable: return 0;
        }
        return 0;
    };
    return rank(left) >= rank(right) ? left : right;
}

bool expressionOperationAddressable(const SctCanonicalExpression& expression,
    std::uint32_t ordinal) {
    const auto* program = std::get_if<SctTypedScptProgram>(&expression.body);
    return program != nullptr && ordinal < program->operations.size();
}

} // namespace

SctControlFlowIndex SctControlFlowIndex::build(const SctDocument& document,
    const SctBoundImportEvidence* evidence) {
    const auto documentIndex = SctDocumentIndex::build(document);
    return buildFromIndex(document, documentIndex, evidence);
}

SctControlFlowIndex SctControlFlowIndex::buildFromIndex(const SctDocument& document,
    const SctDocumentIndex& documentIndex, const SctBoundImportEvidence* evidence) {
    SctControlFlowIndex result;
    for (const auto& section : document.sections) {
        const auto* script = std::get_if<SctScriptSectionContent>(&section.content);
        if (script == nullptr) continue;
        for (std::size_t ordinal = 0; ordinal < script->instructions.size(); ++ordinal) {
            const auto& instruction = script->instructions[ordinal];
            const auto next = ordinal + 1u < script->instructions.size()
                ? std::optional<SctInstructionId>{script->instructions[ordinal + 1u].id} : std::nullopt;
            const auto* schema = findSctOpcodeSchema(instruction.opcode);
            if (schema == nullptr) continue;
            const auto confidence = schema->semantic.confidence;
            switch (schema->semantic.controlRole) {
            case SctOpcodeControlRole::Branch:
                if (schema->parameters.jumpParam >= 0) {
                    addParameterEdge(result.currentEdges_, instruction, SctControlFlowKind::BranchFalse,
                        {static_cast<std::uint32_t>(schema->parameters.jumpParam), std::nullopt}, confidence);
                }
                result.currentEdges_.push_back({instruction.id, SctControlFlowKind::BranchTrue,
                    confidence, std::nullopt, next, std::nullopt, {}});
                break;
            case SctOpcodeControlRole::Switch:
                if (schema->parameters.switchJumpParam >= 0) {
                    for (std::size_t group = 0; group < instruction.repeatedParameterGroups.size(); ++group) {
                        addParameterEdge(result.currentEdges_, instruction, SctControlFlowKind::SwitchCase,
                            {static_cast<std::uint32_t>(schema->parameters.switchJumpParam),
                                static_cast<std::uint32_t>(group)}, confidence);
                    }
                }
                break;
            case SctOpcodeControlRole::Jump:
                if (schema->parameters.jumpParam >= 0) {
                    addParameterEdge(result.currentEdges_, instruction, SctControlFlowKind::Jump,
                        {static_cast<std::uint32_t>(schema->parameters.jumpParam), std::nullopt}, confidence);
                }
                break;
            case SctOpcodeControlRole::CallSubscript:
                addParameterEdge(result.currentEdges_, instruction, SctControlFlowKind::Call,
                    {0u, std::nullopt}, confidence);
                if (next) result.currentEdges_.push_back({instruction.id,
                    SctControlFlowKind::Fallthrough, confidence, std::nullopt, next, std::nullopt, {}});
                break;
            case SctOpcodeControlRole::Return:
                result.currentEdges_.push_back({instruction.id, SctControlFlowKind::Return,
                    confidence, std::nullopt, std::nullopt, std::nullopt, {}});
                break;
            case SctOpcodeControlRole::None:
                if (next) result.currentEdges_.push_back({instruction.id,
                    SctControlFlowKind::Fallthrough, SctSemanticConfidence::Known,
                    std::nullopt, next, std::nullopt, {}});
                break;
            }
        }
    }

    if (evidence == nullptr) {
        result.buildIndexes();
        return result;
    }
    const auto& receipt = evidence->receipt();
    std::vector<ImportedOpaqueSpan> opaqueSpans;
    for (const auto& record : receipt.sourceMap.records()) {
        if (record.role != SctSourceSpanRole::OpaqueAttachment || !record.primaryEntityLocation
            || !record.target) continue;
        const auto* entity = std::get_if<SctDocumentEntityId>(&*record.target);
        const auto* id = entity == nullptr ? nullptr : std::get_if<SctOpaqueAttachmentId>(entity);
        if (id != nullptr) opaqueSpans.push_back({record.span, *id});
    }
    std::ranges::sort(opaqueSpans, {}, [](const ImportedOpaqueSpan& value) {
        return value.span.offset;
    });
    std::vector<SctImportedControlFlowObservation> observations = receipt.controlFlow;
    for (const auto& unresolved : receipt.unresolvedReferences) {
        const bool present = std::any_of(observations.begin(), observations.end(), [&](const auto& item) {
            return item.sourceInstruction == unresolved.sourceInstruction
                && item.origin == std::optional<SctParameterSite>{SctParameterSite{
                    unresolved.sourceInstruction, unresolved.parameter}};
        });
        if (present) continue;
        const auto* instruction = documentIndex.find(document, unresolved.sourceInstruction);
        const auto* schema = instruction == nullptr ? nullptr : findSctOpcodeSchema(instruction->opcode);
        const auto kind = schema == nullptr ? std::nullopt : unresolvedKind(*schema, unresolved.parameter);
        if (!kind) continue;
        std::optional<std::uint32_t> target;
        if (unresolved.calculatedTargetPayloadOffset
            && *unresolved.calculatedTargetPayloadOffset >= 0
            && *unresolved.calculatedTargetPayloadOffset <= std::numeric_limits<std::uint32_t>::max()) {
            target = static_cast<std::uint32_t>(*unresolved.calculatedTargetPayloadOffset);
        }
        observations.push_back({unresolved.sourceInstruction, *kind,
            schema->semantic.confidence, SctParameterSite{unresolved.sourceInstruction,
                unresolved.parameter}, std::nullopt, target});
    }
    for (const auto& observation : observations) {
        result.importedEdges_.push_back({observation.sourceInstruction, observation.kind,
            observation.confidence, observation.origin, observation.targetInstruction,
            observation.targetInstruction ? std::nullopt : observation.targetPayloadOffset,
            crossedAttachments(receipt.sourceMap, opaqueSpans, observation)});
    }
    result.buildIndexes();
    return result;
}

void SctControlFlowIndex::buildIndexes() {
    for (std::size_t index = 0; index < currentEdges_.size(); ++index) {
        const auto& edge = currentEdges_[index];
        currentOutboundIndex_[edge.sourceInstruction].push_back(index);
        if (edge.targetInstruction) currentInboundIndex_[*edge.targetInstruction].push_back(index);
    }
}

std::vector<SctControlFlowEdge> SctControlFlowIndex::currentOutbound(SctInstructionId source) const {
    std::vector<SctControlFlowEdge> result;
    const auto found = currentOutboundIndex_.find(source);
    if (found == currentOutboundIndex_.end()) return result;
    result.reserve(found->second.size());
    for (const auto index : found->second) result.push_back(currentEdges_[index]);
    return result;
}

std::vector<SctControlFlowEdge> SctControlFlowIndex::currentInbound(SctInstructionId target) const {
    std::vector<SctControlFlowEdge> result;
    const auto found = currentInboundIndex_.find(target);
    if (found == currentInboundIndex_.end()) return result;
    result.reserve(found->second.size());
    for (const auto index : found->second) result.push_back(currentEdges_[index]);
    return result;
}

SctInstructionSemanticContribution SctInstructionSemanticAnalyzer::build(
    const SctDocumentInstruction& instruction) {
    SctInstructionSemanticContribution result;
    result.opcode = {instruction.opcode, instruction.id};
    const auto recordExpression = [&](SctInstructionId instruction, SctExpressionOwner owner,
        const SctCanonicalExpression& expression) {
        const SctExpressionSite expressionSite{instruction, owner};
        if (const auto* opaque = std::get_if<SctOpaqueExpression>(&expression.body)) {
            result.opaqueExpressions.push_back({expressionSite, opaque->words.size()});
            return;
        }
        const auto& program = std::get<SctTypedScptProgram>(expression.body);
        for (std::uint32_t ordinal = 0; ordinal < program.operations.size(); ++ordinal) {
            const auto* value = std::get_if<SctScptValueOperation>(&program.operations[ordinal]);
            if (value == nullptr) continue;
            if (const auto kind = variableKind(value->kind)) {
                result.variables.push_back({{*kind, value->encodingWord & 0x00ffffffu},
                    value->kind, {expressionSite, ordinal}});
            }
        }
    };
    const auto recordParameter = [&](SctInstructionId instruction,
        const SctDocumentParameter& parameter, std::optional<std::uint32_t> group) {
        const SctParameterSite site{instruction, {parameter.schemaIndex, group}};
        std::visit([&](const auto& value) {
            using T = std::decay_t<decltype(value)>;
            if constexpr (std::is_same_v<T, SctInstructionReference>) {
                result.references.push_back({site, SctDocumentReferenceTarget{value.target}});
            } else if constexpr (std::is_same_v<T, SctStringReference>) {
                result.references.push_back({site, SctDocumentReferenceTarget{value.target}});
            } else if constexpr (std::is_same_v<T, SctSupplementaryTextReference>) {
                result.references.push_back({site, SctDocumentReferenceTarget{value.target}});
            } else if constexpr (std::is_same_v<T, SctCanonicalExpression>) {
                recordExpression(instruction, site.parameter, value);
            } else if constexpr (std::is_same_v<T, SctUnresolvedReferenceValue>) {
                result.unresolvedReferences.push_back({site, value.expectedTarget,
                    value.encodedWords.size()});
            } else if constexpr (std::is_same_v<T, SctOpaqueParameterValue>) {
                result.opaqueParameters.push_back({site, value.words.size()});
            }
        }, parameter.value);
    };

    if (instruction.scheduledExpression) {
        recordExpression(instruction.id, SctScheduledExpressionSite{},
            *instruction.scheduledExpression);
    }
    for (const auto& parameter : instruction.fixedParameters) {
        recordParameter(instruction.id, parameter, std::nullopt);
    }
    for (std::size_t group = 0; group < instruction.repeatedParameterGroups.size(); ++group) {
        for (const auto& parameter : instruction.repeatedParameterGroups[group].parameters) {
            recordParameter(instruction.id, parameter, static_cast<std::uint32_t>(group));
        }
    }

    const auto* schema = findSctOpcodeSchema(instruction.opcode);
    if (schema == nullptr) return result;
    const auto& rule = schema->semantic.effect;
    switch (rule.kind) {
    case SctOpcodeEffectKind::LoadScript:
    case SctOpcodeEffectKind::LoadMld:
        result.effects.push_back({SctResourceLoadEffect{instruction.id,
            rule.kind == SctOpcodeEffectKind::LoadMld ? SctResourceKind::Mld
                                                      : SctResourceKind::Script,
            {instruction.id, {rule.firstParameter, std::nullopt}}, rule.confidence},
            parameterUsability(instruction, *schema, {rule.firstParameter, std::nullopt})});
        break;
    case SctOpcodeEffectKind::SelectGroundVariant:
        if (rule.secondParameter) {
            result.effects.push_back({SctGroundVariantSelectionEffect{instruction.id,
                {instruction.id, {rule.firstParameter, std::nullopt}},
                {instruction.id, {*rule.secondParameter, std::nullopt}}, rule.confidence},
                combineUsability(
                    parameterUsability(instruction, *schema, {rule.firstParameter, std::nullopt}),
                    parameterUsability(instruction, *schema, {*rule.secondParameter, std::nullopt}))});
        }
        break;
    case SctOpcodeEffectKind::None:
        break;
    }
    return result;
}

namespace {

std::vector<SctInstructionSemanticContribution> semanticContributions(
    const SctDocument& document) {
    std::vector<SctInstructionSemanticContribution> result;
    for (const auto& section : document.sections) {
        const auto* script = std::get_if<SctScriptSectionContent>(&section.content);
        if (script == nullptr) continue;
        for (const auto& instruction : script->instructions) {
            result.push_back(SctInstructionSemanticAnalyzer::build(instruction));
        }
    }
    return result;
}

} // namespace

SctSemanticUsageIndex SctSemanticUsageIndex::build(const SctDocument& document) {
    const auto contributions = semanticContributions(document);
    return build(contributions);
}

SctSemanticUsageIndex SctSemanticUsageIndex::build(
    std::span<const SctInstructionSemanticContribution> contributions) {
    SctSemanticUsageIndex result;
    for (const auto& contribution : contributions) {
        result.opcodeUsages_.push_back(contribution.opcode);
        result.referenceUsages_.insert(result.referenceUsages_.end(),
            contribution.references.begin(), contribution.references.end());
        result.variableUsages_.insert(result.variableUsages_.end(),
            contribution.variables.begin(), contribution.variables.end());
        result.unresolvedReferences_.insert(result.unresolvedReferences_.end(),
            contribution.unresolvedReferences.begin(), contribution.unresolvedReferences.end());
        result.opaqueParameters_.insert(result.opaqueParameters_.end(),
            contribution.opaqueParameters.begin(), contribution.opaqueParameters.end());
        result.opaqueExpressions_.insert(result.opaqueExpressions_.end(),
            contribution.opaqueExpressions.begin(), contribution.opaqueExpressions.end());
    }
    result.buildIndexes();
    return result;
}

void SctSemanticUsageIndex::buildIndexes() {
    for (std::size_t index = 0; index < opcodeUsages_.size(); ++index)
        opcodeIndex_[opcodeUsages_[index].opcode].push_back(index);
    for (std::size_t index = 0; index < referenceUsages_.size(); ++index) {
        outboundReferenceIndex_[referenceUsages_[index].source.instruction].push_back(index);
        inboundReferenceIndex_[referenceUsages_[index].target].push_back(index);
    }
    for (std::size_t index = 0; index < variableUsages_.size(); ++index)
        variableIndex_[variableUsages_[index].variable].push_back(index);
}

std::vector<SctOpcodeUsage> SctSemanticUsageIndex::usagesForOpcode(std::uint16_t opcode) const {
    std::vector<SctOpcodeUsage> result;
    const auto found = opcodeIndex_.find(opcode);
    if (found == opcodeIndex_.end()) return result;
    result.reserve(found->second.size());
    for (const auto index : found->second) result.push_back(opcodeUsages_[index]);
    return result;
}

std::vector<SctReferenceUsage> SctSemanticUsageIndex::outboundReferences(SctInstructionId source) const {
    std::vector<SctReferenceUsage> result;
    const auto found = outboundReferenceIndex_.find(source);
    if (found == outboundReferenceIndex_.end()) return result;
    result.reserve(found->second.size());
    for (const auto index : found->second) result.push_back(referenceUsages_[index]);
    return result;
}

std::vector<SctReferenceUsage> SctSemanticUsageIndex::inboundReferences(
    const SctDocumentReferenceTarget& target) const {
    std::vector<SctReferenceUsage> result;
    const auto found = inboundReferenceIndex_.find(target);
    if (found == inboundReferenceIndex_.end()) return result;
    result.reserve(found->second.size());
    for (const auto index : found->second) result.push_back(referenceUsages_[index]);
    return result;
}

std::vector<SctVariableUsage> SctSemanticUsageIndex::usagesForVariable(
    SctVariableIdentity variable) const {
    std::vector<SctVariableUsage> result;
    const auto found = variableIndex_.find(variable);
    if (found == variableIndex_.end()) return result;
    result.reserve(found->second.size());
    for (const auto index : found->second) result.push_back(variableUsages_[index]);
    return result;
}

SctSupplementaryTextIndex SctSupplementaryTextIndex::build(
    const SctDocument& document) {
    const auto usage = SctSemanticUsageIndex::build(document);
    return buildFromUsage(document, usage);
}

SctSupplementaryTextIndex SctSupplementaryTextIndex::buildFromUsage(
    const SctDocument& document, const SctSemanticUsageIndex& usage) {
    SctSupplementaryTextIndex result;
    result.associations_.reserve(document.supplementaryText.size());
    for (const auto& text : document.supplementaryText) {
        result.associationIndex_.emplace(text.id, result.associations_.size());
        result.associations_.push_back({text.id});
    }

    for (const auto& reference : usage.referenceUsages()) {
        const auto* target = std::get_if<SctSupplementaryTextId>(&reference.target);
        if (target == nullptr) continue;
        result.siteTargets_.emplace(reference.source, *target);
        const auto found = result.associationIndex_.find(*target);
        if (found != result.associationIndex_.end()) {
            result.associations_[found->second].uses.push_back(reference.source);
        }
    }

    for (auto& association : result.associations_) {
        association.useKind = association.uses.empty()
            ? SctSupplementaryTextUseKind::Unreferenced
            : association.uses.size() == 1u
                ? SctSupplementaryTextUseKind::Unique
                : SctSupplementaryTextUseKind::Shared;
    }
    return result;
}

const SctSupplementaryTextAssociation* SctSupplementaryTextIndex::find(
    SctSupplementaryTextId text) const noexcept {
    const auto found = associationIndex_.find(text);
    return found == associationIndex_.end() ? nullptr : &associations_[found->second];
}

std::optional<SctSupplementaryTextId> SctSupplementaryTextIndex::targetFor(
    const SctParameterSite& site) const noexcept {
    const auto found = siteTargets_.find(site);
    return found == siteTargets_.end()
        ? std::nullopt : std::optional<SctSupplementaryTextId>{found->second};
}

const SctDocumentSupplementaryText* SctSupplementaryTextIndex::resolve(
    const SctDocument& document, const SctDocumentIndex& entities,
    const SctParameterSite& site) const noexcept {
    const auto target = targetFor(site);
    return target.has_value() ? entities.find(document, *target) : nullptr;
}

SctOpaqueContextIndex SctOpaqueContextIndex::build(const SctDocument& document,
    const SctBoundImportEvidence* evidence, const SctControlFlowIndex& controlFlow) {
    SctOpaqueContextIndex result;
    if (evidence == nullptr) return result;
    const auto& receipt = evidence->receipt();
    struct CrossingSummary {
        std::vector<SctControlFlowEdgeKey> edges;
        SctSemanticConfidence confidence = SctSemanticConfidence::Known;
        SctSemanticConfidence switchConfidence = SctSemanticConfidence::Known;
        bool hasConfidence = false;
        bool hasSwitch = false;
    };
    std::map<SctOpaqueAttachmentId, CrossingSummary> crossings;
    for (const auto& edge : controlFlow.importedEdges()) {
        for (const auto attachment : edge.crossedOpaqueAttachments) {
            auto& summary = crossings[attachment];
            summary.edges.push_back({edge.sourceInstruction, edge.kind,
                edge.origin, edge.targetInstruction});
            if (!summary.hasConfidence
                || confidenceRank(edge.confidence) < confidenceRank(summary.confidence)) {
                summary.confidence = edge.confidence;
            }
            summary.hasConfidence = true;
            if (edge.kind == SctControlFlowKind::SwitchCase) {
                if (!summary.hasSwitch
                    || confidenceRank(edge.confidence) < confidenceRank(summary.switchConfidence)) {
                    summary.switchConfidence = edge.confidence;
                }
                summary.hasSwitch = true;
            }
        }
    }
    for (const auto& attachment : document.opaqueAttachments) {
        const auto location = receipt.sourceMap.location(SctDocumentEntityId{attachment.id});
        if (!location) continue;
        SctOpaqueContextRecord context{attachment.id, location->primarySpan,
            location->region, location->containingSection,
            receipt.sourceMap.previousSemanticEntity(SctDocumentEntityId{attachment.id}),
            receipt.sourceMap.nextSemanticEntity(SctDocumentEntityId{attachment.id})};
        context.sourceNeighborhood = receipt.sourceMap.neighborhood(location->primarySpan);
        const auto crossing = crossings.find(attachment.id);
        if (crossing != crossings.end()) {
            context.crossingImportedEdges = crossing->second.edges;
            context.interpretations.push_back({SctOpaqueInterpretationKind::ControlFlowGap,
                crossing->second.confidence});
            if (crossing->second.hasSwitch) {
                context.interpretations.push_back({SctOpaqueInterpretationKind::SwitchDispatchGap,
                    crossing->second.switchConfidence});
            }
        }
        result.records_.push_back(std::move(context));
    }
    for (std::size_t index = 0; index < result.records_.size(); ++index)
        result.recordIndex_.emplace(result.records_[index].attachment, index);
    return result;
}

const SctOpaqueContextRecord* SctOpaqueContextIndex::find(SctOpaqueAttachmentId id) const noexcept {
    const auto found = recordIndex_.find(id);
    return found == recordIndex_.end() ? nullptr : &records_[found->second];
}

SctOpcodeEffectIndex SctOpcodeEffectIndex::build(const SctDocument& document) {
    const auto contributions = semanticContributions(document);
    return build(contributions);
}

SctOpcodeEffectIndex SctOpcodeEffectIndex::build(
    std::span<const SctInstructionSemanticContribution> contributions) {
    SctOpcodeEffectIndex result;
    for (const auto& contribution : contributions) {
        result.effects_.insert(result.effects_.end(),
            contribution.effects.begin(), contribution.effects.end());
    }
    result.buildIndexes();
    return result;
}

void SctOpcodeEffectIndex::buildIndexes() {
    for (std::size_t index = 0; index < effects_.size(); ++index) {
        const auto source = std::visit([](const auto& value) { return value.sourceInstruction; },
            effects_[index].effect);
        instructionIndex_[source].push_back(index);
        if (effects_[index].usability == SctOpcodeEffectUsability::Usable)
            usableIndexes_.push_back(index);
    }
}

std::vector<SctOpcodeEffectOccurrence> SctOpcodeEffectIndex::effectsForInstruction(SctInstructionId id) const {
    std::vector<SctOpcodeEffectOccurrence> result;
    const auto found = instructionIndex_.find(id);
    if (found == instructionIndex_.end()) return result;
    result.reserve(found->second.size());
    for (const auto index : found->second) result.push_back(effects_[index]);
    return result;
}

std::vector<SctOpcodeEffectOccurrence> SctOpcodeEffectIndex::usableEffects() const {
    std::vector<SctOpcodeEffectOccurrence> result;
    result.reserve(usableIndexes_.size());
    for (const auto index : usableIndexes_) result.push_back(effects_[index]);
    return result;
}

SctImportedSiteAddressabilityIndex SctImportedSiteAddressabilityIndex::build(
    const SctDocument& document, const SctDocumentIndex& entities,
    const SctImportedSourceMap& sourceMap) {
    SctImportedSiteAddressabilityIndex result;
    const auto entityExists = [&](const SctDocumentEntityId& entity) {
        return std::visit([&](const auto& id) -> bool {
            using Id = std::decay_t<decltype(id)>;
            if constexpr (std::is_same_v<Id, std::monostate>) return false;
            else return entities.find(document, id) != nullptr;
        }, entity);
    };
    const auto classify = [&](const SctImportedSourceTarget& target) {
        return std::visit([&](const auto& value) -> SctImportedSiteAddressability {
            using T = std::decay_t<decltype(value)>;
            if constexpr (std::is_same_v<T, SctDocumentEntityId>) {
                return entityExists(value) ? SctImportedSiteAddressability::ExactSite
                                           : SctImportedSiteAddressability::MissingEntity;
            } else if constexpr (std::is_same_v<T, SctParameterSite>) {
                const auto* instruction = entities.find(document, value.instruction);
                if (instruction == nullptr) return SctImportedSiteAddressability::MissingEntity;
                return findParameter(*instruction, value.parameter) != nullptr
                    ? SctImportedSiteAddressability::ExactSite
                    : SctImportedSiteAddressability::OwningEntityOnly;
            } else if constexpr (std::is_same_v<T, SctExpressionSite>) {
                const auto* instruction = entities.find(document, value.instruction);
                if (instruction == nullptr) return SctImportedSiteAddressability::MissingEntity;
                if (const auto* parameterOwner = std::get_if<SctParameterAddress>(&value.owner)) {
                    const auto* parameter = findParameter(*instruction, *parameterOwner);
                    if (parameter == nullptr) return SctImportedSiteAddressability::OwningEntityOnly;
                    const auto* expression = std::get_if<SctCanonicalExpression>(&parameter->value);
                    return expression == nullptr ? SctImportedSiteAddressability::OwningEntityOnly
                                                 : SctImportedSiteAddressability::ExactSite;
                }
                return instruction->scheduledExpression
                    ? SctImportedSiteAddressability::ExactSite
                    : SctImportedSiteAddressability::OwningEntityOnly;
            } else if constexpr (std::is_same_v<T, SctExpressionOperationSite>) {
                const auto& expressionSite = value.expression;
                const auto* instruction = entities.find(document, expressionSite.instruction);
                if (instruction == nullptr) return SctImportedSiteAddressability::MissingEntity;
                const SctCanonicalExpression* expression = nullptr;
                if (const auto* parameterOwner = std::get_if<SctParameterAddress>(&expressionSite.owner)) {
                    const auto* parameter = findParameter(*instruction, *parameterOwner);
                    if (parameter == nullptr) return SctImportedSiteAddressability::OwningEntityOnly;
                    expression = std::get_if<SctCanonicalExpression>(&parameter->value);
                } else if (instruction->scheduledExpression) {
                    expression = &*instruction->scheduledExpression;
                }
                if (expression == nullptr) return SctImportedSiteAddressability::OwningEntityOnly;
                return expressionOperationAddressable(*expression, value.operationOrdinal)
                    ? SctImportedSiteAddressability::ExactSite
                    : SctImportedSiteAddressability::ParentSiteOnly;
            } else {
                const SctTextValue* text = std::visit([&](const auto& id) -> const SctTextValue* {
                    const auto* entity = entities.find(document, id);
                    return entity == nullptr ? nullptr : &entity->value;
                }, value.text);
                if (text == nullptr) return SctImportedSiteAddressability::MissingEntity;
                if (const auto* plain = std::get_if<SctPlainText>(text)) {
                    if (value.region != SctTextRegion::Body) {
                        return SctImportedSiteAddressability::OwningEntityOnly;
                    }
                    return value.utf8Range.offset + value.utf8Range.size <= plain->utf8.size()
                        ? SctImportedSiteAddressability::ExactSite
                        : SctImportedSiteAddressability::ParentSiteOnly;
                }
                const auto* message = std::get_if<SctMessage>(text);
                if (message == nullptr) {
                    return SctImportedSiteAddressability::OwningEntityOnly;
                }
                if (value.region == SctTextRegion::Header) {
                    if (!message->headerUtf8) {
                        return SctImportedSiteAddressability::OwningEntityOnly;
                    }
                    return value.utf8Range.offset + value.utf8Range.size
                            <= message->headerUtf8->size()
                        ? SctImportedSiteAddressability::ExactSite
                        : SctImportedSiteAddressability::ParentSiteOnly;
                }
                if (!value.elementOrdinal) {
                    return value.utf8Range.size == 0u
                        ? SctImportedSiteAddressability::ExactSite
                        : SctImportedSiteAddressability::ParentSiteOnly;
                }
                if (*value.elementOrdinal >= message->body.elements.size()) {
                    return SctImportedSiteAddressability::ParentSiteOnly;
                }
                const auto* chunk = std::get_if<SctTextChunk>(
                    &message->body.elements[*value.elementOrdinal]);
                if (chunk == nullptr) {
                    return value.utf8Range.size == 0u
                        ? SctImportedSiteAddressability::ExactSite
                        : SctImportedSiteAddressability::ParentSiteOnly;
                }
                return value.utf8Range.offset + value.utf8Range.size <= chunk->utf8.size()
                    ? SctImportedSiteAddressability::ExactSite
                    : SctImportedSiteAddressability::ParentSiteOnly;
            }
        }, target);
    };

    for (const auto& sourceRecord : sourceMap.records()) {
        if (!sourceRecord.target || result.recordIndex_.contains(*sourceRecord.target)) continue;
        const auto index = result.records_.size();
        result.recordIndex_.emplace(*sourceRecord.target, index);
        result.records_.push_back({*sourceRecord.target, classify(*sourceRecord.target)});
    }
    if (result.records_.empty()) {
        result.summary_ = SctImportedAddressabilitySummary::NoSites;
    } else if (std::all_of(result.records_.begin(), result.records_.end(), [](const auto& record) {
                   return record.addressability == SctImportedSiteAddressability::ExactSite;
               })) {
        result.summary_ = SctImportedAddressabilitySummary::FullyAddressable;
    } else if (std::all_of(result.records_.begin(), result.records_.end(), [](const auto& record) {
                   return record.addressability == SctImportedSiteAddressability::MissingEntity;
               })) {
        result.summary_ = SctImportedAddressabilitySummary::NoLongerAddressable;
    } else {
        result.summary_ = SctImportedAddressabilitySummary::PartiallyAddressable;
    }
    return result;
}

const SctImportedSiteAddressabilityRecord* SctImportedSiteAddressabilityIndex::find(
    const SctImportedSourceTarget& target) const noexcept {
    const auto found = recordIndex_.find(target);
    return found == recordIndex_.end() ? nullptr : &records_[found->second];
}

SctDocumentAnalysis SctDocumentAnalysis::build(const SctDocument& document,
    const SctBoundImportEvidence* evidence, const SctAnalysisExecutionOptions execution) {
    SctDocumentAnalysis result;
    result.entities = SctDocumentIndex::build(document);
    result.stringGroups = SctIndexedStringGroupIndex::build(document,
        evidence == nullptr
            ? std::span<const SctImportedIndexedStringGroupObservation>{}
            : std::span<const SctImportedIndexedStringGroupObservation>{
                evidence->receipt().indexedStringGroups});
    result.controlFlow = SctControlFlowIndex::buildFromIndex(
        document, result.entities, evidence);
    const auto contributions = semanticContributions(document);
    result.usage = SctSemanticUsageIndex::build(contributions);
    result.supplementaryText = SctSupplementaryTextIndex::buildFromUsage(
        document, result.usage);
    result.opaqueContext = SctOpaqueContextIndex::build(document, evidence, result.controlFlow);
    result.structuredControlFlow = SctStructuredControlFlowAnalysis::buildFromIndexes(
        document, result.controlFlow, result.opaqueContext, execution);
    result.effects = SctOpcodeEffectIndex::build(contributions);
    if (evidence != nullptr) {
        result.importedSites = SctImportedSiteAddressabilityIndex::build(
            document, result.entities, evidence->receipt().sourceMap);
    }
    return result;
}

} // namespace spice::sct
