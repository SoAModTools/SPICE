#include "../SpiceSCT/SpiceSCT.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <cctype>
#include <limits>
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
    if (result.evidencedProfiles.size() == 1u) return SctRuntimeFactAgreement::SingleProfile;
    for (std::size_t index = 1; index < result.evidencedProfiles.size(); ++index) {
        if (!profileFactsEqual(result.records, result.evidencedProfiles.front(),
            result.evidencedProfiles[index])) return SctRuntimeFactAgreement::Divergent;
    }
    return SctRuntimeFactAgreement::Uniform;
}

SctRuntimeFactCoverage expectedCoverage(const SctOpcodeParameterFactQueryResult& result) {
    if (result.evidencedProfiles.empty()) return SctRuntimeFactCoverage::None;
    return result.evidencedProfiles.size() == result.opcodeAvailableProfiles.size()
        ? SctRuntimeFactCoverage::Complete : SctRuntimeFactCoverage::Partial;
}

const SctOpcodeParameterFactRecord* findRecord(std::string_view id) {
    const auto catalog = sctOpcodeParameterFactCatalog();
    const auto found = std::find_if(catalog.begin(), catalog.end(),
        [id](const auto& record) { return record.sourceRecordId == id; });
    return found == catalog.end() ? nullptr : &*found;
}

template <typename Fact>
const Fact* findFact(const SctOpcodeParameterFactRecord& record) {
    for (const auto& behavior : record.behaviors) {
        for (const auto& payload : behavior.facts) {
            if (const auto* fact = std::get_if<Fact>(&payload)) return fact;
        }
    }
    return nullptr;
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

    const auto* gcJp = findSctRuntimeProfile(SctRuntimeProfileId::GameCubeJpRetail20021112);
    const auto* gcEu = findSctRuntimeProfile(SctRuntimeProfileId::GameCubeEuRetail20030305);
    const auto* dcEu = findSctRuntimeProfile(SctRuntimeProfileId::DreamcastEuRetailDisc1);
    ASSERT_NE(nullptr, gcJp);
    ASSERT_NE(nullptr, gcEu);
    ASSERT_NE(nullptr, dcEu);
    EXPECT_EQ(SctPlatform::GameCube, gcJp->platform);
    EXPECT_EQ("6A8212FEA4D21E40BB294D4563A2B4A339D50506A14D87566CB239A1547953C6",
        gcJp->executableSha256);
    EXPECT_EQ(SctPlatform::GameCube, gcEu->platform);
    EXPECT_EQ("32B83047D12FEC618A435A8015AD5F72DF0B354A8850A88EE759B266D1D044EB",
        gcEu->executableSha256);
    EXPECT_EQ(SctPlatform::Dreamcast, dcEu->platform);
    EXPECT_EQ("F09FA463E8BCB50196056D0BEE4331E94A3770158C39FAFE2BE97BE3045BBB68",
        dcEu->executableSha256);
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
        if (record.resolution
            == SctRuntimeFactResolution::ProducerConfirmedFinalConsumerUnresolved) {
            EXPECT_TRUE(std::all_of(record.behaviors.begin(), record.behaviors.end(),
                [](const auto& behavior) {
                    return std::all_of(behavior.evidence.begin(), behavior.evidence.end(),
                        [](const auto& evidence) {
                            return evidence.confidence.structural == SctRuntimeFactConfidence::High
                                && evidence.confidence.semantic == SctRuntimeFactConfidence::Low;
                        });
                }));
        }
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
                EXPECT_EQ(SctRuntimeFactCoverage::Complete, result.coverage);
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
        EXPECT_EQ(expectedCoverage(result), result.coverage);
    }
}

TEST(SctOpcodeParameterFactsContract, CoverageIsIndependentFromBehaviorAgreement) {
    const auto op16All = SctOpcodeParameterFacts::query(16, 0,
        SctRuntimeFactTarget{SctAllRegisteredRuntimeProfiles{}});
    EXPECT_EQ(SctRuntimeFactQueryStatus::Available, op16All.status);
    EXPECT_EQ(SctRuntimeFactCoverage::Partial, op16All.coverage);
    EXPECT_EQ(SctRuntimeFactAgreement::Uniform, op16All.agreement);
    EXPECT_EQ(2u, op16All.evidencedProfiles.size());

    const auto op16GcJp = SctOpcodeParameterFacts::query(16, 0,
        SctRuntimeFactTarget{SctRuntimeProfileId::GameCubeJpRetail20021112});
    EXPECT_EQ(SctRuntimeFactQueryStatus::NoConfirmedFacts, op16GcJp.status);
    EXPECT_EQ(SctRuntimeFactCoverage::None, op16GcJp.coverage);
    EXPECT_EQ(SctRuntimeFactAgreement::NoEvidence, op16GcJp.agreement);
    ASSERT_EQ(1u, op16GcJp.opcodeAvailableProfiles.size());

    const auto op54All = SctOpcodeParameterFacts::query(54, 0,
        SctRuntimeFactTarget{SctAllRegisteredRuntimeProfiles{}});
    EXPECT_EQ(SctRuntimeFactCoverage::Partial, op54All.coverage);
    EXPECT_EQ(SctRuntimeFactAgreement::Divergent, op54All.agreement);

    const auto op54DcEu = SctOpcodeParameterFacts::query(54, 0,
        SctRuntimeFactTarget{SctRuntimeProfileId::DreamcastEuRetailDisc1});
    EXPECT_EQ(SctRuntimeFactCoverage::Complete, op54DcEu.coverage);
    EXPECT_EQ(SctRuntimeFactAgreement::SingleProfile, op54DcEu.agreement);

    const auto op265All = SctOpcodeParameterFacts::query(265, 0,
        SctRuntimeFactTarget{SctAllRegisteredRuntimeProfiles{}});
    EXPECT_EQ(3u, op265All.opcodeAvailableProfiles.size());
    EXPECT_EQ(1u, op265All.evidencedProfiles.size());
    EXPECT_EQ(SctRuntimeFactCoverage::Partial, op265All.coverage);
    EXPECT_EQ(SctRuntimeFactAgreement::SingleProfile, op265All.agreement);

    const auto op265Dreamcast = SctOpcodeParameterFacts::query(265, 0,
        SctRuntimeFactTarget{SctPlatform::Dreamcast});
    EXPECT_EQ(SctRuntimeFactQueryStatus::UnavailableForTarget, op265Dreamcast.status);
}

TEST(SctOpcodeParameterFactsContract, RepresentativeResolvedV3FactsRemainTyped) {
    const auto* op16 = findRecord("sct-param-v2:016:0");
    ASSERT_NE(nullptr, op16);
    const auto* op16Special = findFact<SctSpecialEncodedValueFact>(*op16);
    ASSERT_NE(nullptr, op16Special);
    ASSERT_TRUE(op16Special->replacement.has_value());
    EXPECT_EQ(SctRuntimeScalarValue{std::int64_t{std::numeric_limits<std::int32_t>::max()}},
        *op16Special->replacement);

    const auto* op17 = findRecord("sct-param-v2:017:0");
    ASSERT_NE(nullptr, op17);
    std::vector<const SctRuntimeValueFlowFact*> bitFlows;
    for (const auto& behavior : op17->behaviors) {
        for (const auto& payload : behavior.facts) {
            if (const auto* candidate = std::get_if<SctRuntimeValueFlowFact>(&payload)) {
                bitFlows.push_back(candidate);
            }
        }
    }
    ASSERT_EQ(2u, bitFlows.size());
    ASSERT_EQ(2u, bitFlows[0]->steps.size());
    ASSERT_EQ(2u, bitFlows[1]->steps.size());
    EXPECT_TRUE(std::holds_alternative<SctRuntimeArithmeticFact>(bitFlows[0]->steps[1]));
    EXPECT_TRUE(std::holds_alternative<SctBitMaskFact>(bitFlows[1]->steps[1]));

    const auto* op41 = findRecord("sct-param-v3:041:0");
    ASSERT_NE(nullptr, op41);
    const auto* compareFlow = findFact<SctRuntimeValueFlowFact>(*op41);
    ASSERT_NE(nullptr, compareFlow);
    ASSERT_EQ(3u, compareFlow->steps.size());
    EXPECT_TRUE(std::holds_alternative<SctRuntimeComparisonFact>(compareFlow->steps[1]));
    EXPECT_TRUE(std::holds_alternative<SctBitMaskFact>(compareFlow->steps[2]));

    const auto* op207 = findRecord("sct-param-v3:207:2");
    ASSERT_NE(nullptr, op207);
    const auto* cap = findFact<SctNumericDomainFact>(*op207);
    ASSERT_NE(nullptr, cap);
    EXPECT_EQ(SctNumericDomainAuthority::RuntimeEnforced, cap->authority);

    const auto* op221 = findRecord("sct-param-v2:221:0");
    ASSERT_NE(nullptr, op221);
    const auto* disposition = findFact<SctRuntimeValueDispositionFact>(*op221);
    ASSERT_NE(nullptr, disposition);
    EXPECT_EQ(SctRuntimeValueDispositionKind::EvaluatedAndDiscarded,
        disposition->disposition);

    const auto* unresolved = findRecord("sct-param-v3:030:1");
    ASSERT_NE(nullptr, unresolved);
    EXPECT_EQ(SctRuntimeFactResolution::ProducerConfirmedFinalConsumerUnresolved,
        unresolved->resolution);
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
            EXPECT_EQ(SctRuntimeFactCoverage::None, result.coverage);
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
