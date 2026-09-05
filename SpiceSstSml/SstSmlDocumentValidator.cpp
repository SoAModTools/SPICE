#include "SstSmlDocumentValidator.h"

#include "SstParser.h"
#include "../SpiceMLD/MldDocumentValidator.h"

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
    std::string message,
    std::optional<SmlEmbeddedResourceId> embeddedResourceId = std::nullopt) {
    result.diagnostics.push_back(SstSmlDocumentDiagnostic{
        severity, source, std::move(message), std::nullopt, embeddedResourceId });
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

std::uint32_t valueWidth(const SstCommandFieldValue& value) {
    return std::visit([](const auto item) -> std::uint32_t { return sizeof(item); }, value);
}

std::uint32_t catalogWidth(const detail::CommandFieldWidth width) {
    using detail::CommandFieldWidth;
    switch (width) {
    case CommandFieldWidth::I8:
    case CommandFieldWidth::U8: return 1U;
    case CommandFieldWidth::I16:
    case CommandFieldWidth::U16: return 2U;
    case CommandFieldWidth::U32:
    case CommandFieldWidth::F32: return 4U;
    }
    return 0U;
}

bool specialized(const std::int16_t type, const detail::CommandFieldSummary& field) {
    if (type == 1) return true;
    return type == 0 && field.offset >= 0x1CU && field.offset < 0x40U;
}

bool claim(std::vector<bool>& ownership,
    const std::uint32_t offset,
    const std::uint32_t size,
    SstSmlDocumentValidationResult& result,
    const char* kind) {
    if (offset > ownership.size() || size > ownership.size() - offset) {
        addDiagnostic(result, SstSmlDiagnosticSeverity::Error, SstSmlSourceMember::Sst,
            std::string(kind) + " extends beyond the recognized command payload");
        return false;
    }
    if (std::any_of(ownership.begin() + offset, ownership.begin() + offset + size,
            [](const bool owned) { return owned; })) {
        addDiagnostic(result, SstSmlDiagnosticSeverity::Error, SstSmlSourceMember::Sst,
            std::string(kind) + " overlaps another command-payload owner");
        return false;
    }
    std::fill(ownership.begin() + offset, ownership.begin() + offset + size, true);
    return true;
}

} // namespace

SstSmlDocumentValidationResult SstSmlDocumentValidator::validate(
    const SstSmlDocument& document,
    const SstSmlDocumentImportReceipt* receipt) {
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
        if (const auto* opaque = std::get_if<SmlOpaqueEmbeddedResource>(&member.sml.resource.content)) {
            if (opaque->bytes.empty()) {
                addDiagnostic(result, SstSmlDiagnosticSeverity::Error, SstSmlSourceMember::Sml,
                    "Opaque SML embedded resource is empty");
            }
        } else if (receipt) {
            const auto* nestedReceipt = receipt->embeddedMld(member.sml.resource.id);
            if (!nestedReceipt) {
                addDiagnostic(result, SstSmlDiagnosticSeverity::Error, SstSmlSourceMember::Sml,
                    "Decoded embedded MLD is missing its keyed import receipt", member.sml.resource.id);
            } else {
                const auto target = spice::mld::MldWriteTarget{ nestedReceipt->platform, nestedReceipt->wrapper };
                const auto nested = spice::mld::MldDocumentValidator::validate(
                    std::get<spice::mld::MldDocument>(member.sml.resource.content), target, nestedReceipt);
                for (const auto& diagnostic : nested.diagnostics) {
                    addDiagnostic(result,
                        diagnostic.severity == spice::mld::MldDiagnosticSeverity::Error
                            ? SstSmlDiagnosticSeverity::Error : SstSmlDiagnosticSeverity::Warning,
                        SstSmlSourceMember::Sml,
                        "Embedded MLD: " + diagnostic.message,
                        member.sml.resource.id);
                }
            }
        }
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
            auto expectedFields = SstParser::fieldSummariesForType(command.type);
            std::erase_if(expectedFields, [&](const auto& field) { return specialized(command.type, field); });
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
                if (fieldIndex < expectedFields.size() && field.payloadOffset != expectedFields[fieldIndex].offset) {
                    addDiagnostic(result, SstSmlDiagnosticSeverity::Error, SstSmlSourceMember::Sst,
                        "SST command field offset disagrees with the supported command catalog");
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
            if (!command.lightingRows.empty() && command.type != 1) {
                addDiagnostic(result, SstSmlDiagnosticSeverity::Error, SstSmlSourceMember::Sst,
                    "Only SST command type 1 may own lighting rows");
            }
            for (const auto& row : command.lightingRows) {
                insertId(lightingRowIds, row.id.value, result, "SST lighting row");
            }
            if (command.type == 1 && command.lightingRows.empty()) {
                addDiagnostic(result, SstSmlDiagnosticSeverity::Error, SstSmlSourceMember::Sst,
                    "SST command type 1 is missing its canonical lighting rows");
            }
            if (known) {
                std::vector<bool> ownership(SstParser::commandPayloadSize(command.type), false);
                for (const auto& field : command.fields) {
                    claim(ownership, field.payloadOffset, valueWidth(field.value), result,
                        "SST command field");
                }
                if (command.placement) claim(ownership, 0x1CU, 0x24U, result, "SST placement");
                if (command.type == 1) {
                    const auto rowFields = SstParser::fieldSummariesForType(1);
                    for (std::size_t row = 0U; row < command.lightingRows.size(); ++row) {
                        const auto base = static_cast<std::uint32_t>(row * 0x68U);
                        for (const auto& field : rowFields) {
                            claim(ownership, base + field.offset, catalogWidth(field.width), result,
                                "SST lighting-row field");
                        }
                    }
                }
                for (const auto& fragment : command.opaquePayloadFragments) {
                    insertId(opaqueIds, fragment.id.value, result, "opaque block");
                    if (fragment.bytes.empty()) {
                        addDiagnostic(result, SstSmlDiagnosticSeverity::Error, SstSmlSourceMember::Sst,
                            "SST command contains an empty opaque payload fragment");
                    } else {
                        claim(ownership, fragment.payloadOffset,
                            static_cast<std::uint32_t>(fragment.bytes.size()), result,
                            "SST command opaque fragment");
                    }
                }
                if (std::any_of(ownership.begin(), ownership.end(), [](const bool owned) { return !owned; })) {
                    addDiagnostic(result, SstSmlDiagnosticSeverity::Error, SstSmlSourceMember::Sst,
                        "SST recognized command payload does not have complete semantic-or-opaque ownership");
                }
            } else if (!command.opaquePayloadFragments.empty()) {
                addDiagnostic(result, SstSmlDiagnosticSeverity::Error, SstSmlSourceMember::Sst,
                    "Unknown SST command cannot own payload fragments with an unproved span");
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

    if (receipt) {
        std::set<std::uint64_t> receiptIds;
        for (const auto& nested : receipt->embeddedMlds) {
            if (!receiptIds.insert(nested.resourceId.value).second) {
                addDiagnostic(result, SstSmlDiagnosticSeverity::Error, SstSmlSourceMember::Sml,
                    "Embedded MLD receipt IDs are not unique");
            }
            if (!resourceIds.contains(nested.resourceId.value)) {
                addDiagnostic(result, SstSmlDiagnosticSeverity::Error, SstSmlSourceMember::Sml,
                    "Embedded MLD receipt refers to an unknown SML resource");
                continue;
            }
            const auto member = std::find_if(document.members.begin(), document.members.end(), [&](const auto& value) {
                return value.sml.resource.id == nested.resourceId;
            });
            if (member != document.members.end() &&
                std::holds_alternative<SmlOpaqueEmbeddedResource>(member->sml.resource.content)) {
                addDiagnostic(result, SstSmlDiagnosticSeverity::Error, SstSmlSourceMember::Sml,
                    "Opaque SML embedded resource must not have an MLD import receipt");
            }
        }
    }

    if (!hasErrors(result)) result.readiness = SstSmlDocumentReadiness::Valid;
    return result;
}

const char* toString(SstSmlDocumentReadiness readiness) noexcept {
    switch (readiness) {
    case SstSmlDocumentReadiness::Invalid: return "invalid";
    case SstSmlDocumentReadiness::Valid: return "valid";
    }
    return "unknown";
}

} // namespace spice::sstsml
