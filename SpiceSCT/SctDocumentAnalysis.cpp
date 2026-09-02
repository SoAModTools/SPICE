#include "SctDocumentAnalysis.h"

#include "SctOpcodeMetadata.h"

#include <algorithm>
#include <functional>
#include <limits>
#include <set>
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

std::vector<SctOpaqueAttachmentId> crossedAttachments(const SctImportedSourceMap& sourceMap,
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

    for (const auto& record : sourceMap.records()) {
        if (record.role != SctSourceSpanRole::OpaqueAttachment || !record.primaryEntityLocation
            || !record.target) continue;
        const auto* entity = std::get_if<SctDocumentEntityId>(&*record.target);
        const auto* id = entity == nullptr ? nullptr : std::get_if<SctOpaqueAttachmentId>(entity);
        if (id == nullptr) continue;
        if (record.span.endOffset() > low && record.span.offset < high
            && std::find(result.begin(), result.end(), *id) == result.end()) {
            result.push_back(*id);
        }
    }
    return result;
}

std::optional<SctVariableKind> variableKind(SctCanonicalExpressionNodeKind kind) {
    switch (kind) {
    case SctCanonicalExpressionNodeKind::IntVariable:
    case SctCanonicalExpressionNodeKind::NegatedIntVariable:
    case SctCanonicalExpressionNodeKind::NegatedIntVariableLow16Comparison:
        return SctVariableKind::Integer;
    case SctCanonicalExpressionNodeKind::FloatVariable: return SctVariableKind::Float;
    case SctCanonicalExpressionNodeKind::BitVariable: return SctVariableKind::Bit;
    case SctCanonicalExpressionNodeKind::ByteVariable: return SctVariableKind::Byte;
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

SctSemanticConfidence weakestConfidence(std::span<const SctControlFlowEdge> edges,
    SctOpaqueAttachmentId attachment, bool switchesOnly) {
    auto result = SctSemanticConfidence::Known;
    bool found = false;
    for (const auto& edge : edges) {
        if (switchesOnly && edge.kind != SctControlFlowKind::SwitchCase) continue;
        if (std::find(edge.crossedOpaqueAttachments.begin(), edge.crossedOpaqueAttachments.end(), attachment)
            == edge.crossedOpaqueAttachments.end()) continue;
        if (!found || confidenceRank(edge.confidence) < confidenceRank(result)) result = edge.confidence;
        found = true;
    }
    return found ? result : SctSemanticConfidence::Unknown;
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
        return std::holds_alternative<SctOpaqueExpression>(expression->root)
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
                : std::holds_alternative<SctFooterEntryReference>(value->value));
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

const SctCanonicalExpression* expressionAt(const SctDocumentInstruction& instruction,
    const SctExpressionOwner& owner) {
    if (std::holds_alternative<SctScheduledExpressionSite>(owner)) {
        return instruction.scheduledExpression ? &*instruction.scheduledExpression : nullptr;
    }
    const auto* parameter = findParameter(instruction, std::get<SctParameterAddress>(owner));
    return parameter == nullptr ? nullptr : std::get_if<SctCanonicalExpression>(&parameter->value);
}

const SctCanonicalExpressionNode* expressionNodeAt(const SctCanonicalExpression& expression,
    std::span<const std::uint32_t> path) {
    const auto* node = std::get_if<SctCanonicalExpressionNode>(&expression.root);
    for (const auto child : path) {
        if (node == nullptr || child >= node->children.size()) return nullptr;
        node = &node->children[child];
    }
    return node;
}

bool expressionSiteAddressable(const SctCanonicalExpression& expression,
    std::span<const std::uint32_t> path) {
    if (path.empty()) return true;
    return expressionNodeAt(expression, path) != nullptr;
}

} // namespace

SctControlFlowIndex SctControlFlowIndex::build(const SctDocument& document,
    const SctBoundImportEvidence* evidence) {
    SctControlFlowIndex result;
    const auto documentIndex = SctDocumentIndex::build(document);
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

    if (evidence == nullptr) return result;
    const auto& receipt = evidence->receipt();
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
            crossedAttachments(receipt.sourceMap, observation)});
    }
    return result;
}

std::vector<SctControlFlowEdge> SctControlFlowIndex::currentOutbound(SctInstructionId source) const {
    std::vector<SctControlFlowEdge> result;
    for (const auto& edge : currentEdges_) if (edge.sourceInstruction == source) result.push_back(edge);
    return result;
}

std::vector<SctControlFlowEdge> SctControlFlowIndex::currentInbound(SctInstructionId target) const {
    std::vector<SctControlFlowEdge> result;
    for (const auto& edge : currentEdges_) if (edge.targetInstruction == target) result.push_back(edge);
    return result;
}

SctSemanticUsageIndex SctSemanticUsageIndex::build(const SctDocument& document) {
    SctSemanticUsageIndex result;
    std::function<void(SctInstructionId, const SctExpressionOwner&,
        const SctCanonicalExpressionNode&, std::vector<std::uint32_t>&)> recordNode;
    recordNode = [&](SctInstructionId instruction, const SctExpressionOwner& owner,
        const SctCanonicalExpressionNode& node, std::vector<std::uint32_t>& path) {
        if (const auto kind = variableKind(node.kind)) {
            result.variableUsages_.push_back({{*kind, node.encodingCode & 0x00ffffffu},
                node.kind, {instruction, owner, path}});
        }
        for (std::size_t child = 0; child < node.children.size(); ++child) {
            path.push_back(static_cast<std::uint32_t>(child));
            recordNode(instruction, owner, node.children[child], path);
            path.pop_back();
        }
    };
    const auto recordExpression = [&](SctInstructionId instruction, SctExpressionOwner owner,
        const SctCanonicalExpression& expression) {
        if (const auto* opaque = std::get_if<SctOpaqueExpression>(&expression.root)) {
            result.opaqueExpressions_.push_back({{instruction, std::move(owner), {}}, opaque->words.size()});
            return;
        }
        std::vector<std::uint32_t> path;
        recordNode(instruction, owner,
            std::get<SctCanonicalExpressionNode>(expression.root), path);
    };
    const auto recordParameter = [&](SctInstructionId instruction,
        const SctDocumentParameter& parameter, std::optional<std::uint32_t> group) {
        const SctParameterSite site{instruction, {parameter.schemaIndex, group}};
        std::visit([&](const auto& value) {
            using T = std::decay_t<decltype(value)>;
            if constexpr (std::is_same_v<T, SctInstructionReference>) {
                result.referenceUsages_.push_back({site, SctDocumentReferenceTarget{value.target}});
            } else if constexpr (std::is_same_v<T, SctStringReference>) {
                result.referenceUsages_.push_back({site, SctDocumentReferenceTarget{value.target}});
            } else if constexpr (std::is_same_v<T, SctFooterEntryReference>) {
                result.referenceUsages_.push_back({site, SctDocumentReferenceTarget{value.target}});
            } else if constexpr (std::is_same_v<T, SctCanonicalExpression>) {
                recordExpression(instruction, site.parameter, value);
            } else if constexpr (std::is_same_v<T, SctUnresolvedReferenceValue>) {
                result.unresolvedReferences_.push_back({site, value.expectedTarget,
                    value.encodedWords.size()});
            } else if constexpr (std::is_same_v<T, SctOpaqueParameterValue>) {
                result.opaqueParameters_.push_back({site, value.words.size()});
            }
        }, parameter.value);
    };

    for (const auto& section : document.sections) {
        const auto* script = std::get_if<SctScriptSectionContent>(&section.content);
        if (script == nullptr) continue;
        for (const auto& instruction : script->instructions) {
            result.opcodeUsages_.push_back({instruction.opcode, instruction.id});
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
        }
    }
    return result;
}

std::vector<SctOpcodeUsage> SctSemanticUsageIndex::usagesForOpcode(std::uint16_t opcode) const {
    std::vector<SctOpcodeUsage> result;
    for (const auto& usage : opcodeUsages_) if (usage.opcode == opcode) result.push_back(usage);
    return result;
}

std::vector<SctReferenceUsage> SctSemanticUsageIndex::outboundReferences(SctInstructionId source) const {
    std::vector<SctReferenceUsage> result;
    for (const auto& usage : referenceUsages_) if (usage.source.instruction == source) result.push_back(usage);
    return result;
}

std::vector<SctReferenceUsage> SctSemanticUsageIndex::inboundReferences(
    const SctDocumentReferenceTarget& target) const {
    std::vector<SctReferenceUsage> result;
    for (const auto& usage : referenceUsages_) if (usage.target == target) result.push_back(usage);
    return result;
}

std::vector<SctVariableUsage> SctSemanticUsageIndex::usagesForVariable(
    SctVariableIdentity variable) const {
    std::vector<SctVariableUsage> result;
    for (const auto& usage : variableUsages_) if (usage.variable == variable) result.push_back(usage);
    return result;
}

SctOpaqueContextIndex SctOpaqueContextIndex::build(const SctDocument& document,
    const SctBoundImportEvidence* evidence, const SctControlFlowIndex& controlFlow) {
    SctOpaqueContextIndex result;
    if (evidence == nullptr) return result;
    const auto& receipt = evidence->receipt();
    for (const auto& attachment : document.opaqueAttachments) {
        const auto location = receipt.sourceMap.location(SctDocumentEntityId{attachment.id});
        if (!location) continue;
        SctOpaqueContextRecord context{attachment.id, location->primarySpan,
            location->region, location->containingSection,
            receipt.sourceMap.previousSemanticEntity(SctDocumentEntityId{attachment.id}),
            receipt.sourceMap.nextSemanticEntity(SctDocumentEntityId{attachment.id})};
        bool hasSwitch = false;
        for (const auto& edge : controlFlow.importedEdges()) {
            if (std::find(edge.crossedOpaqueAttachments.begin(), edge.crossedOpaqueAttachments.end(),
                    attachment.id) == edge.crossedOpaqueAttachments.end()) continue;
            context.crossingImportedEdges.push_back({edge.sourceInstruction, edge.kind,
                edge.origin, edge.targetInstruction});
            hasSwitch = hasSwitch || edge.kind == SctControlFlowKind::SwitchCase;
        }
        if (!context.crossingImportedEdges.empty()) {
            context.interpretations.push_back({SctOpaqueInterpretationKind::ControlFlowGap,
                weakestConfidence(controlFlow.importedEdges(), attachment.id, false)});
        }
        if (hasSwitch) {
            context.interpretations.push_back({SctOpaqueInterpretationKind::SwitchDispatchGap,
                weakestConfidence(controlFlow.importedEdges(), attachment.id, true)});
        }
        result.records_.push_back(std::move(context));
    }
    return result;
}

const SctOpaqueContextRecord* SctOpaqueContextIndex::find(SctOpaqueAttachmentId id) const noexcept {
    const auto found = std::find_if(records_.begin(), records_.end(),
        [&](const auto& record) { return record.attachment == id; });
    return found == records_.end() ? nullptr : &*found;
}

SctOpcodeEffectIndex SctOpcodeEffectIndex::build(const SctDocument& document) {
    SctOpcodeEffectIndex result;
    for (const auto& section : document.sections) {
        const auto* script = std::get_if<SctScriptSectionContent>(&section.content);
        if (script == nullptr) continue;
        for (const auto& instruction : script->instructions) {
            const auto* schema = findSctOpcodeSchema(instruction.opcode);
            if (schema == nullptr) continue;
            const auto& rule = schema->semantic.effect;
            switch (rule.kind) {
            case SctOpcodeEffectKind::LoadScript:
            case SctOpcodeEffectKind::LoadMld:
                result.effects_.push_back({SctResourceLoadEffect{instruction.id,
                    rule.kind == SctOpcodeEffectKind::LoadMld ? SctResourceKind::Mld
                                                              : SctResourceKind::Script,
                    {instruction.id, {rule.firstParameter, std::nullopt}}, rule.confidence},
                    parameterUsability(instruction, *schema, {rule.firstParameter, std::nullopt})});
                break;
            case SctOpcodeEffectKind::SelectGroundVariant:
                if (rule.secondParameter) {
                    result.effects_.push_back({SctGroundVariantSelectionEffect{instruction.id,
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
        }
    }
    return result;
}

std::vector<SctOpcodeEffectOccurrence> SctOpcodeEffectIndex::effectsForInstruction(SctInstructionId id) const {
    std::vector<SctOpcodeEffectOccurrence> result;
    for (const auto& occurrence : effects_) {
        const auto source = std::visit([](const auto& value) { return value.sourceInstruction; },
            occurrence.effect);
        if (source == id) result.push_back(occurrence);
    }
    return result;
}

std::vector<SctOpcodeEffectOccurrence> SctOpcodeEffectIndex::usableEffects() const {
    std::vector<SctOpcodeEffectOccurrence> result;
    for (const auto& occurrence : effects_) {
        if (occurrence.usability == SctOpcodeEffectUsability::Usable) result.push_back(occurrence);
    }
    return result;
}

SctDocumentAnalysis SctDocumentAnalysis::build(const SctDocument& document,
    const SctBoundImportEvidence* evidence) {
    SctDocumentAnalysis result;
    result.entities = SctDocumentIndex::build(document);
    result.controlFlow = SctControlFlowIndex::build(document, evidence);
    result.usage = SctSemanticUsageIndex::build(document);
    result.opaqueContext = SctOpaqueContextIndex::build(document, evidence, result.controlFlow);
    result.effects = SctOpcodeEffectIndex::build(document);
    if (evidence != nullptr) {
        std::size_t total = 0;
        std::size_t addressable = 0;
        const auto targetAddressable = [&](const SctImportedSourceTarget& target) {
            return std::visit([&](const auto& value) -> bool {
                using T = std::decay_t<decltype(value)>;
                if constexpr (std::is_same_v<T, SctDocumentEntityId>) {
                    return std::visit([&](const auto& id) -> bool {
                        using Id = std::decay_t<decltype(id)>;
                        if constexpr (std::is_same_v<Id, std::monostate>) return false;
                        else return result.entities.find(document, id) != nullptr;
                    }, value);
                } else if constexpr (std::is_same_v<T, SctParameterSite>) {
                    const auto* instruction = result.entities.find(document, value.instruction);
                    return instruction != nullptr && findParameter(*instruction, value.parameter) != nullptr;
                } else if constexpr (std::is_same_v<T, SctExpressionSite>) {
                    const auto* instruction = result.entities.find(document, value.instruction);
                    if (instruction == nullptr) return false;
                    const auto* expression = expressionAt(*instruction, value.owner);
                    return expression != nullptr && expressionSiteAddressable(*expression, value.childPath);
                } else {
                    const SctTextValue* text = std::visit([&](const auto& id) -> const SctTextValue* {
                        const auto* entity = result.entities.find(document, id);
                        return entity == nullptr ? nullptr : &entity->value;
                    }, value.text);
                    if (text == nullptr) return false;
                    if (const auto* plain = std::get_if<SctPlainText>(text)) {
                        return value.region == SctTextRegion::Body
                            && value.utf8Range.offset + value.utf8Range.size <= plain->utf8.size();
                    }
                    const auto* message = std::get_if<SctMessage>(text);
                    if (message == nullptr) return value.utf8Range.size == 0u;
                    if (value.region == SctTextRegion::Header) {
                        return message->headerUtf8
                            && value.utf8Range.offset + value.utf8Range.size <= message->headerUtf8->size();
                    }
                    if (!value.elementOrdinal) return value.utf8Range.size == 0u;
                    if (*value.elementOrdinal >= message->body.elements.size()) return false;
                    const auto* chunk = std::get_if<SctTextChunk>(
                        &message->body.elements[*value.elementOrdinal]);
                    return chunk == nullptr ? value.utf8Range.size == 0u
                        : value.utf8Range.offset + value.utf8Range.size <= chunk->utf8.size();
                }
            }, target);
        };
        std::set<SctImportedSourceTarget> observed;
        for (const auto& record : evidence->receipt().sourceMap.records()) {
            if (!record.target || !observed.insert(*record.target).second) continue;
            ++total;
            if (targetAddressable(*record.target)) ++addressable;
        }
        result.importedSiteAddressability = total != 0u && addressable == total
            ? ImportedSiteAddressability::FullyAddressable
            : addressable == 0u ? ImportedSiteAddressability::NoLongerAddressable
                                : ImportedSiteAddressability::PartiallyAddressable;
    }
    return result;
}

} // namespace spice::sct
