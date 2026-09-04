#include "../SpiceSCT/SpiceSCT.h"
#include "../SpiceSCT/SctSha256.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdlib>
#include <ranges>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

namespace {

using namespace spice::sct;
using Clock = std::chrono::steady_clock;

SctDocument makePerformanceDocument() {
    SctDocument document;
    constexpr std::uint32_t sectionCount = 32u;
    constexpr std::uint32_t instructionsPerSection = 256u;
    for (std::uint32_t sectionOrdinal = 0; sectionOrdinal < sectionCount; ++sectionOrdinal) {
        std::vector<SctDocumentInstruction> instructions;
        instructions.reserve(instructionsPerSection);
        for (std::uint32_t instructionOrdinal = 1u;
             instructionOrdinal < instructionsPerSection; ++instructionOrdinal) {
            instructions.push_back({document.allocateInstructionId(), 125u});
        }
        instructions.push_back({document.allocateInstructionId(), 12u});
        document.sections.push_back({document.allocateSectionId(),
            "P" + std::to_string(sectionOrdinal),
            SctScriptSectionContent{std::move(instructions)}});
    }
    return document;
}

template <typename Operation>
auto measured(Operation&& operation) {
    const auto begin = Clock::now();
    auto result = operation();
    const auto elapsed = std::chrono::duration_cast<std::chrono::microseconds>(
        Clock::now() - begin);
    return std::pair{std::move(result), elapsed};
}

bool performanceTestsEnabled() {
    char* value = nullptr;
    std::size_t length = 0u;
    if (_dupenv_s(&value, &length, "SPICE_SCT_RUN_PERFORMANCE") != 0 || value == nullptr) {
        return false;
    }
    const bool enabled = std::string_view{value} == "1";
    std::free(value);
    return enabled;
}

} // namespace

TEST(SctSha256, IncrementalHashMatchesKnownDigestAndOneShotHash) {
    constexpr std::array<std::uint8_t, 32> expected{
        0xbau, 0x78u, 0x16u, 0xbfu, 0x8fu, 0x01u, 0xcfu, 0xeau,
        0x41u, 0x41u, 0x40u, 0xdeu, 0x5du, 0xaeu, 0x22u, 0x23u,
        0xb0u, 0x03u, 0x61u, 0xa3u, 0x96u, 0x17u, 0x7au, 0x9cu,
        0xb4u, 0x10u, 0xffu, 0x61u, 0xf2u, 0x00u, 0x15u, 0xadu,
    };
    constexpr std::array<std::uint8_t, 3> input{'a', 'b', 'c'};
    EXPECT_EQ(spice::sct::detail::sha256(input), expected);

    spice::sct::detail::Sha256 incremental;
    incremental.update(std::span<const std::uint8_t>{input}.first(1u));
    incremental.update(std::span<const std::uint8_t>{input}.subspan(1u));
    EXPECT_EQ(incremental.finish(), expected);
    EXPECT_EQ(incremental.finish(), expected);
}

// This harness is deliberately opt-in and records observations rather than
// asserting machine-dependent timing thresholds. Run it in an optimized build
// with SPICE_SCT_RUN_PERFORMANCE=1 and the SctPerformance.* filter.
TEST(SctPerformance, OptInSyntheticPipelineStageTimings) {
    if (!performanceTestsEnabled()) {
        GTEST_SKIP() << "Set SPICE_SCT_RUN_PERFORMANCE=1 to run the SCT timing harness.";
    }

    auto document = makePerformanceDocument();
    const auto options = SctDocumentExportOptions{SctPlatform::GameCube,
        kSctShiftJisByte7FEncoding, SctDocumentOutputByteOrder::BigEndian,
        SctDocumentOutputWrapper::Raw,
        SctOpaquePreservationPolicy::RequirePreservation};

    auto [neutral, neutralTime] = measured([&] {
        return SctDocumentValidator::validateDocument(document);
    });
    ASSERT_TRUE(neutral.validDocument);
    ASSERT_TRUE(neutral.receipt.has_value());

    auto [target, targetTime] = measured([&] {
        return SctDocumentValidator::validateForTarget(document,
            options.targetPlatform, options.textEncoding, nullptr, &*neutral.receipt);
    });
    ASSERT_TRUE(target.validForTarget);
    ASSERT_TRUE(target.receipt.has_value());

    auto [exported, exportTime] = measured([&] {
        return SctDocumentExporter::exportDocument(
            document, options, nullptr, &*target.receipt);
    });
    ASSERT_TRUE(exported.success);

    auto [parsed, parseTime] = measured([&] {
        return SctParser{}.parse(exported.bytes, "synthetic_performance.sct");
    });
    ASSERT_TRUE(parsed.parseOk);

    auto [imported, importTime] = measured([&] {
        return SctDocumentImporter::import(parsed, {{SctPlatform::GameCube}});
    });
    ASSERT_TRUE(imported.document.has_value());

    auto [sequential, sequentialTime] = measured([&] {
        return SctDocumentAnalysis::build(document, nullptr,
            SctAnalysisExecutionOptions{1u});
    });
    const auto availableConcurrency = std::max(1u, std::thread::hardware_concurrency());
    auto [parallel, parallelTime] = measured([&] {
        return SctDocumentAnalysis::build(document, nullptr,
            SctAnalysisExecutionOptions{availableConcurrency});
    });
    ASSERT_TRUE(std::ranges::equal(sequential.structuredControlFlow.sections(),
        parallel.structuredControlFlow.sections()));

    RecordProperty("neutral_validation_us", neutralTime.count());
    RecordProperty("target_validation_us", targetTime.count());
    RecordProperty("export_us", exportTime.count());
    RecordProperty("parse_us", parseTime.count());
    RecordProperty("import_us", importTime.count());
    RecordProperty("analysis_sequential_us", sequentialTime.count());
    RecordProperty("analysis_parallel_us", parallelTime.count());
    RecordProperty("analysis_parallel_limit", availableConcurrency);
    RecordProperty("decoded_payload_bytes", exported.decodedPayloadSize);
}
