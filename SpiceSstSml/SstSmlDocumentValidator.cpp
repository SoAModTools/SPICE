#include "SstSmlDocumentValidator.h"

#include "SstParser.h"

#include <algorithm>
#include <limits>
#include <set>
#include <string>
#include <type_traits>

namespace spice::sstsml {
namespace {

using detail::SstParser;

void addDiagnostic(SstSmlDocumentValidationResult& result,
    SstSmlDiagnosticSeverity severity,
    SstSmlSourceMember source,
    std::string message) {
    result.diagnostics.push_back(SstSmlDocumentDiagnostic{
        severity, source, std::move(message), std::nullopt });
}

bool insertId(std::set<std::uint64_t>& ids,
    std::uint64_t value,
    SstSmlDocumentValidationResult& result,
    const char* kind) {
    if (value == 0U) {
        addDiagnostic(result, SstSmlDiagnosticSeverity::Error, SstSmlSourceMember::Pair,
            std::string(kind) + " ID is zero");
        return false;
    }
    if (!ids.insert(value).second) {
        addDiagnostic(result, SstSmlDiagnosticSeverity::Error, SstSmlSourceMember::Pair,
            std::string(kind) + " IDs are not unique");
        return false;
    }
    return true;
}

bool hasErrors(const SstSmlDocumentValidationResult& result) {
    return std::any_of(result.diagnostics.begin(), result.diagnostics.end(), [](const auto& diagnostic) {
        return diagnostic.severity == SstSmlDiagnosticSeverity::Error;
    });
}

bool valueMatchesWidth(const SstCommandFieldValue& value, detail::CommandFieldWidth width) {
    using detail::CommandFieldWidth;
    switch (width) {
    case CommandFieldWidth::I8: return std::holds_alternative<std::int8_t>(value);
    case CommandFieldWidth::U8: return std::holds_alternative<std::uint8_t>(value);
    case CommandFieldWidth::I16: return std::holds_alternative<std::int16_t>(value);
    case CommandFieldWidth::U16: return std::holds_alternative<std::uint16_t>(value);
    case CommandFieldWidth::U32: return std::holds_alternative<std::uint32_t>(value);
    case CommandFieldWidth::F32: return std::holds_alternative<float>(value);
    }
    return false;
}

template <typename Value>
std::optional<Value> fieldValue(const SstStageCommand& command, const char* name) {
    const auto found = std::find_if(command.fields.begin(), command.fields.end(), [&](const auto& field) {
        return field.name == name;
    });
    if (found == command.fields.end()) return std::nullopt;
    if (const auto value = std::get_if<Value>(&found->value)) return *value;
    return std::nullopt;
}

bool placementAgrees(const SstStageCommand& command) {
    if (!command.placement) return false;
    const auto& placement = *command.placement;
    return fieldValue<float>(command, "transformPositionX") == placement.positionX &&
        fieldValue<float>(command, "transformPositionY") == placement.positionY &&
        fieldValue<float>(command, "transformPositionZ") == placement.positionZ &&
        fieldValue<std::uint32_t>(command, "rotationAngleX") == placement.rotationAngleX &&
        fieldValue<std::uint32_t>(command, "rotationAngleY") == placement.rotationAngleY &&
        fieldValue<std::uint32_t>(command, "rotationAngleZ") == placement.rotationAngleZ &&
        fieldValue<float>(command, "scaleX") == placement.scaleX &&
        fieldValue<float>(command, "scaleY") == placement.scaleY &&
        fieldValue<float>(command, "scaleZ") == placement.scaleZ;
}

bool firstLightingRowAgrees(const SstStageCommand& command) {
    if (command.lightingRows.empty()) return false;
    const auto& row = command.lightingRows.front();
    return fieldValue<std::int8_t>(command, "rowState") == row.state &&
        fieldValue<std::int16_t>(command, "classSelector") == row.classSelector &&
        fieldValue<std::uint32_t>(command, "lightingFlags") == row.flags &&
        fieldValue<std::int16_t>(command, "runtimeSlotId") == row.runtimeSlotId &&
        fieldValue<float>(command, "lightVectorX") == row.lightVector[0] &&
        fieldValue<float>(command, "lightVectorY") == row.lightVector[1] &&
        fieldValue<float>(command, "lightVectorZ") == row.lightVector[2] &&
        fieldValue<float>(command, "slotRgbR") == row.slotRgb[0] &&
        fieldValue<float>(command, "slotRgbG") == row.slotRgb[1] &&
        fieldValue<float>(command, "slotRgbB") == row.slotRgb[2] &&
        fieldValue<float>(command, "globalRgbR") == row.globalRgb[0] &&
        fieldValue<float>(command, "globalRgbG") == row.globalRgb[1] &&
        fieldValue<float>(command, "globalRgbB") == row.globalRgb[2] &&
        fieldValue<float>(command, "attenuationOrSpot0") == row.attenuationOrSpot0 &&
        fieldValue<float>(command, "attenuationOrSpot1") == row.attenuationOrSpot1 &&
        fieldValue<std::uint32_t>(command, "rowTailWord") == row.rawTailWord;
}

} // namespace

SstSmlDocumentValidationResult SstSmlDocumentValidator::validate(
    const SstSmlDocument& document) {
    SstSmlDocumentValidationResult result{};
    if (document.members.empty()) {
        addDiagnostic(result, SstSmlDiagnosticSeverity::Error, SstSmlSourceMember::Pair,
            "A battle-stage document must contain at least one stage member");
    }
    if (document.members.size() > std::numeric_limits<std::uint16_t>::max()) {
        addDiagnostic(result, SstSmlDiagnosticSeverity::Error, SstSmlSourceMember::Pair,
            "Stage-member count exceeds the format's 16-bit count field");
    }
    if (document.stageHeaderSentinel != 0xFFFFU) {
        addDiagnostic(result, SstSmlDiagnosticSeverity::Warning, SstSmlSourceMember::Pair,
            "Stage-header sentinel is not the canonical 0xFFFF value");
    }
    if (document.recordCountSentinel != 0xFFFFU) {
        addDiagnostic(result, SstSmlDiagnosticSeverity::Warning, SstSmlSourceMember::Pair,
            "Record-count sentinel is not the canonical 0xFFFF value");
    }

    std::set<std::uint64_t> memberIds;
    std::set<std::uint64_t> smlRecordIds;
    std::set<std::uint64_t> resourceIds;
    std::set<std::uint64_t> sstRecordIds;
    std::set<std::uint64_t> blockIds;
    std::set<std::uint64_t> commandIds;
    std::set<std::uint64_t> fieldIds;
    std::set<std::uint64_t> placementIds;
    std::set<std::uint64_t> lightingRowIds;
    std::set<std::uint64_t> terrainIds;
    std::set<std::uint64_t> opaqueIds;
    for (std::size_t index = 0U; index < document.members.size(); ++index) {
        const auto& member = document.members[index];
        insertId(memberIds, member.id.value, result, "stage member");
        insertId(smlRecordIds, member.sml.id.value, result, "SML record");
        insertId(resourceIds, member.sml.resource.id.value, result, "SML embedded resource");
        insertId(sstRecordIds, member.sst.id.value, result, "SST record");
        insertId(blockIds, member.sst.commandBlock.id.value, result, "SST command block");
        if (index == 0U) {
            if (member.sst.previousCommandBlockLength.has_value() || member.sst.reservedWord.has_value()) {
                addDiagnostic(result, SstSmlDiagnosticSeverity::Error, SstSmlSourceMember::Sst,
                    "SST record 0 duplicates the paired header instead of using later-record metadata");
            }
        } else if (!member.sst.previousCommandBlockLength.has_value() || !member.sst.reservedWord.has_value()) {
            addDiagnostic(result, SstSmlDiagnosticSeverity::Error, SstSmlSourceMember::Sst,
                "Every SST record after record 0 requires previous-block and reserved metadata");
        }
        if (member.sst.recordIndexWord != index) {
            addDiagnostic(result, SstSmlDiagnosticSeverity::Warning, SstSmlSourceMember::Sst,
                "SST encoded record index does not match document order");
        }
        if (member.sml.resourceIndexWord != index) {
            addDiagnostic(result, SstSmlDiagnosticSeverity::Warning, SstSmlSourceMember::Sml,
                "SML encoded resource index does not match document order");
        }
        if (member.sst.commandBlock.sentinelType >= 0) {
            addDiagnostic(result, SstSmlDiagnosticSeverity::Error, SstSmlSourceMember::Sst,
                "SST command-block sentinel type must be negative");
        }
        for (const auto& command : member.sst.commandBlock.commands) {
            insertId(commandIds, command.id.value, result, "SST command");
            const auto known = SstParser::isKnownCommandType(command.type);
            if (command.payloadSpanKnown != known) {
                addDiagnostic(result, SstSmlDiagnosticSeverity::Error, SstSmlSourceMember::Sst,
                    "SST command payload-span status disagrees with the supported command catalog");
            }
            if (known && command.payloadBytes.size() != SstParser::commandPayloadSize(command.type)) {
                addDiagnostic(result, SstSmlDiagnosticSeverity::Error, SstSmlSourceMember::Sst,
                    "SST command payload size disagrees with the supported command catalog");
            }
            const auto expectedFields = SstParser::fieldSummariesForType(command.type);
            if (command.fields.size() != expectedFields.size()) {
                addDiagnostic(result, SstSmlDiagnosticSeverity::Error, SstSmlSourceMember::Sst,
                    "SST command typed-field count disagrees with the supported command catalog");
            }
            for (std::size_t fieldIndex = 0U; fieldIndex < command.fields.size(); ++fieldIndex) {
                const auto& field = command.fields[fieldIndex];
                insertId(fieldIds, field.id.value, result, "SST command field");
                if (fieldIndex < expectedFields.size() && field.name != expectedFields[fieldIndex].name) {
                    addDiagnostic(result, SstSmlDiagnosticSeverity::Error, SstSmlSourceMember::Sst,
                        "SST command typed-field ordering disagrees with the supported command catalog");
                }
                if (fieldIndex < expectedFields.size() &&
                    !valueMatchesWidth(field.value, expectedFields[fieldIndex].width)) {
                    addDiagnostic(result, SstSmlDiagnosticSeverity::Error, SstSmlSourceMember::Sst,
                        "SST command field value type disagrees with its encoded width");
                }
            }
            if (command.placement.has_value()) {
                insertId(placementIds, command.placement->id.value, result, "SST placement");
                if (command.type != 0) {
                    addDiagnostic(result, SstSmlDiagnosticSeverity::Error, SstSmlSourceMember::Sst,
                        "Only SST command type 0 may own a placement");
                }
            } else if (command.type == 0) {
                addDiagnostic(result, SstSmlDiagnosticSeverity::Error, SstSmlSourceMember::Sst,
                    "SST command type 0 is missing its typed placement");
            }
            if (command.type == 0 && command.placement && !placementAgrees(command)) {
                addDiagnostic(result, SstSmlDiagnosticSeverity::Error, SstSmlSourceMember::Sst,
                    "SST command type 0 placement disagrees with its typed command fields");
            }
            if (!command.lightingRows.empty() && command.type != 1) {
                addDiagnostic(result, SstSmlDiagnosticSeverity::Error, SstSmlSourceMember::Sst,
                    "Only SST command type 1 may own lighting rows");
            }
            for (const auto& row : command.lightingRows) {
                insertId(lightingRowIds, row.id.value, result, "SST lighting row");
            }
            if (command.type == 1 && !firstLightingRowAgrees(command)) {
                addDiagnostic(result, SstSmlDiagnosticSeverity::Error, SstSmlSourceMember::Sst,
                    "SST command type 1 lighting rows disagree with its typed command fields");
            }
        }
        if (member.sst.commandBlock.battleGrid) {
            insertId(terrainIds, member.sst.commandBlock.battleGrid->id.value, result,
                "SST battle-grid terrain");
        }
        if (member.sst.commandBlock.trailingOpaque) {
            insertId(opaqueIds, member.sst.commandBlock.trailingOpaque->id.value, result,
                "opaque block");
            if (member.sst.commandBlock.trailingOpaque->bytes.empty()) {
                addDiagnostic(result, SstSmlDiagnosticSeverity::Error, SstSmlSourceMember::Sst,
                    "SST command block contains an empty trailing opaque block");
            }
        }
    }

    std::set<std::uint64_t> seenResources;
    for (const auto& item : document.smlBodyLayout) {
        std::visit([&](const auto& value) {
            using Item = std::decay_t<decltype(value)>;
            if constexpr (std::is_same_v<Item, SmlEmbeddedResourceId>) {
                if (!resourceIds.contains(value.value)) {
                    addDiagnostic(result, SstSmlDiagnosticSeverity::Error, SstSmlSourceMember::Sml,
                        "SML body layout refers to an unknown embedded resource");
                } else if (!seenResources.insert(value.value).second) {
                    addDiagnostic(result, SstSmlDiagnosticSeverity::Error, SstSmlSourceMember::Sml,
                        "SML body layout refers to an embedded resource more than once");
                }
            } else {
                insertId(opaqueIds, value.id.value, result, "opaque block");
                if (value.bytes.empty()) {
                    addDiagnostic(result, SstSmlDiagnosticSeverity::Error, SstSmlSourceMember::Sml,
                        "SML body layout contains an empty opaque block");
                }
            }
        }, item);
    }
    if (seenResources.size() != resourceIds.size()) {
        addDiagnostic(result, SstSmlDiagnosticSeverity::Error, SstSmlSourceMember::Sml,
            "SML body layout does not own every embedded resource exactly once");
    }

    std::set<std::uint64_t> seenBlocks;
    for (const auto& item : document.sstBodyLayout) {
        std::visit([&](const auto& value) {
            using Item = std::decay_t<decltype(value)>;
            if constexpr (std::is_same_v<Item, SstCommandBlockId>) {
                if (!blockIds.contains(value.value)) {
                    addDiagnostic(result, SstSmlDiagnosticSeverity::Error, SstSmlSourceMember::Sst,
                        "SST body layout refers to an unknown command block");
                } else if (!seenBlocks.insert(value.value).second) {
                    addDiagnostic(result, SstSmlDiagnosticSeverity::Error, SstSmlSourceMember::Sst,
                        "SST body layout refers to a command block more than once");
                }
            } else {
                insertId(opaqueIds, value.id.value, result, "opaque block");
                if (value.bytes.empty()) {
                    addDiagnostic(result, SstSmlDiagnosticSeverity::Error, SstSmlSourceMember::Sst,
                        "SST body layout contains an empty opaque block");
                }
            }
        }, item);
    }
    if (seenBlocks.size() != blockIds.size()) {
        addDiagnostic(result, SstSmlDiagnosticSeverity::Error, SstSmlSourceMember::Sst,
            "SST body layout does not own every command block exactly once");
    }

    if (!hasErrors(result)) result.readiness = SstSmlDocumentReadiness::ReadOnly;
    return result;
}

const char* toString(SstSmlDocumentReadiness readiness) noexcept {
    switch (readiness) {
    case SstSmlDocumentReadiness::Invalid: return "invalid";
    case SstSmlDocumentReadiness::ReadOnly: return "read_only";
    }
    return "unknown";
}

} // namespace spice::sstsml
