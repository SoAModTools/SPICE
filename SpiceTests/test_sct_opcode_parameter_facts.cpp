#include "../SpiceSCT/SpiceSCT.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <cctype>
#include <set>
#include <string_view>

namespace {
using namespace spice::sct;

bool validSha256(std::string_view value) {
    return value.size() == 64u && std::all_of(value.begin(), value.end(), [](unsigned char c) {
        return std::isxdigit(c) != 0;
    });
}

bool behaviorContainsOnly(const SctProfiledRuntimeBehavior& behavior,
    SctRuntimeProfileId profile) {
    return behavior.profiles.size() == 1u && behavior.profiles.front() == profile
        && std::all_of(behavior.evidence.begin(), behavior.evidence.end(),
            [profile](const auto& evidence) { return evidence.profile == profile; });
}

bool profileFactsEqual(std::span<const SctOpcodeParameterFactRecord> records,
    SctRuntimeProfileId left, SctRuntimeProfileId right) {
    for (const auto& record : records) {
        const SctProfiledRuntimeBehavior* leftBehavior = nullptr;
        const SctProfiledRuntimeBehavior* rightBehavior = nullptr;
        for (const auto& behavior : record.behaviors) {
            if (std::find(behavior.profiles.begin(), behavior.profiles.end(), left)
                != behavior.profiles.end()) leftBehavior = &behavior;
            if (std::find(behavior.profiles.begin(), behavior.profiles.end(), right)
                != behavior.profiles.end()) rightBehavior = &behavior;
        }
        if ((leftBehavior == nullptr) != (rightBehavior == nullptr)) return false;
        if (leftBehavior != nullptr && (leftBehavior->stage != rightBehavior->stage
            || leftBehavior->facts != rightBehavior->facts)) return false;
    }
    return true;
}

SctRuntimeFactAgreement expectedAgreement(const SctOpcodeParameterFactQueryResult& result) {
    if (result.records.empty()) return SctRuntimeFactAgreement::NoEvidence;
    if (result.availableProfiles.size() < result.selectedProfiles.size()) {
        return SctRuntimeFactAgreement::PartialAvailability;
    }
    if (result.availableProfiles.size() == 1u) return SctRuntimeFactAgreement::SingleProfile;
    for (std::size_t index = 1; index < result.availableProfiles.size(); ++index) {
        if (!profileFactsEqual(result.records, result.availableProfiles.front(),
            result.availableProfiles[index])) return SctRuntimeFactAgreement::Divergent;
    }
    return SctRuntimeFactAgreement::Uniform;
}

SctDocument makeOutOfConventionDocument() {
    SctDocument document;
    SctDocumentInstruction edited;
    edited.id = document.allocateInstructionId();
    edited.opcode = 253;
    edited.fixedParameters = {
        {0u, SctExpressionFactory::encodedDecimalLiteral(4)},
        {1u, SctExpressionFactory::encodedDecimalLiteral(7)},
        {2u, SctExpressionFactory::encodedDecimalLiteral(250)},
    };
    SctDocumentInstruction stop;
    stop.id = document.allocateInstructionId();
    stop.opcode = 12;
    document.sections.push_back({document.allocateSectionId(), "SCRIPT",
        SctScriptSectionContent{{std::move(edited), std::move(stop)}}});
    return document;
}
} // namespace

TEST(SctOpcodeParameterFactsContract, ProfilesAreStableAndSelfDescribing) {
    const auto profiles = sctRuntimeProfiles();
    ASSERT_FALSE(profiles.empty());
    std::set<SctRuntimeProfileId> ids;
    for (const auto& profile : profiles) {
        EXPECT_TRUE(ids.insert(profile.id).second);
        EXPECT_TRUE(validSha256(profile.executableSha256));
        EXPECT_FALSE(profile.label.empty());
        EXPECT_FALSE(profile.evidenceScope.empty());
        EXPECT_EQ(&profile, findSctRuntimeProfile(profile.id));
    }
}

TEST(SctOpcodeParameterFactsContract, CatalogRecordsReferenceValidSchemaAndProfiles) {
    const auto parameters = sctOpcodeParameterFactCatalog();
    const auto opcodes = sctOpcodeRuntimeFactCatalog();
    ASSERT_FALSE(parameters.empty());
    ASSERT_FALSE(opcodes.empty());

    std::set<std::string_view> ids;
    for (const auto& record : parameters) {
        EXPECT_TRUE(ids.insert(record.sourceRecordId).second);
        const auto* schema = findSctOpcodeSchema(record.opcode);
        ASSERT_NE(nullptr, schema) << record.sourceRecordId;
        EXPECT_NE(nullptr, sctOpcodeParameterSchema(*schema, record.schemaIndex))
            << record.sourceRecordId;
        ASSERT_FALSE(record.behaviors.empty());
        for (const auto& behavior : record.behaviors) {
            ASSERT_FALSE(behavior.profiles.empty());
            ASSERT_FALSE(behavior.facts.empty());
            for (const auto profile : behavior.profiles) {
                EXPECT_NE(nullptr, findSctRuntimeProfile(profile));
            }
            for (const auto& evidence : behavior.evidence) {
                EXPECT_TRUE(std::find(behavior.profiles.begin(), behavior.profiles.end(),
                    evidence.profile) != behavior.profiles.end());
            }
        }
    }
    for (const auto& record : opcodes) {
        EXPECT_TRUE(ids.insert(record.sourceRecordId).second);
        EXPECT_NE(nullptr, findSctOpcodeSchema(record.opcode));
        EXPECT_FALSE(record.stateKey.empty());
        EXPECT_FALSE(record.behaviors.empty());
    }
}

TEST(SctOpcodeParameterFactsContract, ExactProfileQueriesFilterApplicabilityAndEvidence) {
    for (const auto& source : sctOpcodeParameterFactCatalog()) {
        for (const auto& sourceBehavior : source.behaviors) {
            for (const auto profile : sourceBehavior.profiles) {
                const auto result = SctOpcodeParameterFacts::query(source.opcode,
                    source.schemaIndex, SctRuntimeFactTarget{profile});
                if (result.status == SctRuntimeFactQueryStatus::UnavailableForTarget) continue;
                ASSERT_EQ(SctRuntimeFactQueryStatus::Available, result.status);
                EXPECT_EQ(SctRuntimeFactAgreement::SingleProfile, result.agreement);
                ASSERT_EQ(1u, result.selectedProfiles.size());
                EXPECT_EQ(profile, result.selectedProfiles.front());
                for (const auto& record : result.records) {
                    for (const auto& behavior : record.behaviors) {
                        EXPECT_TRUE(behaviorContainsOnly(behavior, profile));
                    }
                }
            }
        }
    }
}

TEST(SctOpcodeParameterFactsContract, AggregateQueriesClassifyBehaviorWithoutFreezingFacts) {
    for (const auto& source : sctOpcodeParameterFactCatalog()) {
        const auto result = SctOpcodeParameterFacts::query(source.opcode,
            source.schemaIndex, SctRuntimeFactTarget{SctAllRegisteredRuntimeProfiles{}});
        EXPECT_EQ(expectedAgreement(result), result.agreement);
    }
}

TEST(SctOpcodeParameterFactsContract, QuerySeparatesErrorsFromMissingResearch) {
    EXPECT_EQ(SctRuntimeFactQueryStatus::UnknownOpcode,
        SctOpcodeParameterFacts::query(999, 0,
            SctRuntimeFactTarget{SctAllRegisteredRuntimeProfiles{}}).status);
    EXPECT_EQ(SctRuntimeFactQueryStatus::UnknownParameter,
        SctOpcodeParameterFacts::query(0, 99,
            SctRuntimeFactTarget{SctAllRegisteredRuntimeProfiles{}}).status);

    bool foundUnresearchedParameter = false;
    for (const auto& schema : sctOpcodeSchemas()) {
        for (std::uint32_t index = 0; index < schema.parameterCatalogCount; ++index) {
            const auto hasRecord = std::any_of(sctOpcodeParameterFactCatalog().begin(),
                sctOpcodeParameterFactCatalog().end(), [&](const auto& record) {
                    return record.opcode == schema.opcode && record.schemaIndex == index;
                });
            if (hasRecord || schema.gameCubeAvailability != SctOpcodeAvailability::Available) continue;
            const auto result = SctOpcodeParameterFacts::query(schema.opcode, index,
                SctRuntimeFactTarget{SctRuntimeProfileId::GameCubeUsRetail20021219});
            EXPECT_EQ(SctRuntimeFactQueryStatus::NoConfirmedFacts, result.status);
            EXPECT_EQ(SctRuntimeFactAgreement::NoEvidence, result.agreement);
            foundUnresearchedParameter = true;
            break;
        }
        if (foundUnresearchedParameter) break;
    }
    EXPECT_TRUE(foundUnresearchedParameter);
}

TEST(SctOpcodeParameterFactsContract, OpcodeStateUsesItsSiblingQuery) {
    for (const auto& source : sctOpcodeRuntimeFactCatalog()) {
        const auto result = SctOpcodeRuntimeFacts::query(source.opcode,
            SctRuntimeFactTarget{SctAllRegisteredRuntimeProfiles{}});
        EXPECT_EQ(SctRuntimeFactQueryStatus::Available, result.status);
        EXPECT_FALSE(result.records.empty());
        EXPECT_TRUE(std::all_of(result.records.begin(), result.records.end(),
            [&](const auto& record) { return record.opcode == source.opcode; }));
    }
}

TEST(SctOpcodeParameterFactsContract, InformationalDomainsDoNotAffectValidationOrExport) {
    const auto document = makeOutOfConventionDocument();
    EXPECT_TRUE(SctDocumentValidator::validateDocument(document).validDocument);
    EXPECT_TRUE(SctDocumentValidator::validateForTarget(document, SctPlatform::GameCube,
        kSctShiftJisByte7FEncoding).validForTarget);
    const SctDocumentExportOptions options{SctPlatform::GameCube,
        kSctShiftJisByte7FEncoding, SctDocumentOutputByteOrder::BigEndian,
        SctDocumentOutputWrapper::Raw, SctOpaquePreservationPolicy::RequirePreservation};
    const auto exported = SctDocumentExporter::exportDocument(document, options);
    ASSERT_TRUE(exported.success);
    EXPECT_TRUE(SctParser{}.parse(exported.bytes, "runtime-facts-nonenforcing.sct").parseOk);
}

TEST(SctOpcodeParameterFactsContract, StructuralRulesRemainInTheAuthoritativeSchema) {
    for (const auto& schema : sctOpcodeSchemas()) {
        for (std::uint32_t index = 0; index < schema.parameterCatalogCount; ++index) {
            const auto* parameter = sctOpcodeParameterSchema(schema, index);
            ASSERT_NE(nullptr, parameter);
            if (parameter->encoding == SctOpcodeParameterEncoding::RawWordsUntilSentinel) {
                EXPECT_TRUE(parameter->terminator.has_value());
            } else {
                EXPECT_FALSE(parameter->terminator.has_value());
            }
        }
    }
}
