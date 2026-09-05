#include "../Compression/Aklz.h"
#include "../SpiceSCT/SpiceSCT.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <cstdint>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

namespace {

using namespace spice::sct;

void writeU32(std::vector<std::uint8_t>& bytes, const std::size_t offset,
    const std::uint32_t value) {
    bytes[offset] = static_cast<std::uint8_t>(value >> 24u);
    bytes[offset + 1u] = static_cast<std::uint8_t>(value >> 16u);
    bytes[offset + 2u] = static_cast<std::uint8_t>(value >> 8u);
    bytes[offset + 3u] = static_cast<std::uint8_t>(value);
}

void appendU32(std::vector<std::uint8_t>& bytes, const std::uint32_t value) {
    const auto offset = bytes.size();
    bytes.resize(offset + 4u);
    writeU32(bytes, offset, value);
}

std::vector<std::uint8_t> makeSct(std::vector<std::uint8_t> section,
    const std::uint32_t sectionStart = 0u) {
    std::vector<std::uint8_t> bytes(32u, 0u);
    writeU32(bytes, 8u, 1u);
    writeU32(bytes, 12u, sectionStart);
    const std::string name = "MAIN";
    std::copy(name.begin(), name.end(), bytes.begin() + 16);
    bytes.insert(bytes.end(), section.begin(), section.end());
    return bytes;
}

std::vector<std::uint8_t> cleanSct() {
    std::vector<std::uint8_t> section;
    appendU32(section, 9u);
    appendU32(section, 0x04000000u);
    appendU32(section, 0x3f800000u);
    appendU32(section, 0x1du);
    appendU32(section, 12u);
    return makeSct(std::move(section));
}

TEST(SctParserDiagnostics, CleanParseHasNoDiagnosticsAndNoConsoleOutput) {
    std::ostringstream stdoutCapture;
    std::ostringstream stderrCapture;
    auto* oldStdout = std::cout.rdbuf(stdoutCapture.rdbuf());
    auto* oldStderr = std::cerr.rdbuf(stderrCapture.rdbuf());
    const auto parsed = SctParser{}.parse(cleanSct(), "clean.sct");
    std::cout.rdbuf(oldStdout);
    std::cerr.rdbuf(oldStderr);

    ASSERT_TRUE(parsed.parseOk);
    EXPECT_TRUE(parsed.diagnostics.empty());
    EXPECT_TRUE(stdoutCapture.str().empty());
    EXPECT_TRUE(stderrCapture.str().empty());
}

TEST(SctParserDiagnostics, StructuralFailuresAreErrors) {
    const auto empty = SctParser{}.parse({}, "empty.sct");
    ASSERT_FALSE(empty.parseOk);
    ASSERT_FALSE(empty.diagnostics.empty());
    EXPECT_EQ(empty.diagnostics.front().severity, SctDiagnosticSeverity::Error);

    std::vector<std::uint8_t> oneWord;
    appendU32(oneWord, 12u);
    const auto invalidBounds = SctParser{}.parse(makeSct(oneWord, 8u), "bounds.sct");
    ASSERT_FALSE(invalidBounds.parseOk);
    EXPECT_TRUE(std::ranges::any_of(invalidBounds.diagnostics, [](const auto& diagnostic) {
        return diagnostic.severity == SctDiagnosticSeverity::Error
            && diagnostic.message.find("Invalid section bounds") != std::string::npos;
    }));

    const std::vector<std::uint8_t> truncatedAklz = {
        'A', 'K', 'L', 'Z', '~', '?', 'Q', 'd', '=', 0xccu, 0xccu, 0xcdu,
    };
    const auto compressed = SctParser{}.parse(truncatedAklz, "truncated.aklz");
    ASSERT_FALSE(compressed.parseOk);
    ASSERT_FALSE(compressed.diagnostics.empty());
    EXPECT_EQ(compressed.diagnostics.front().severity, SctDiagnosticSeverity::Error);
}

TEST(SctParserDiagnostics, RecoverableConditionsAreWarnings) {
    std::vector<std::uint8_t> section;
    appendU32(section, 12u);
    const auto parsed = SctParser{}.parse(makeSct(std::move(section)), "missing_label.sct");
    ASSERT_TRUE(parsed.parseOk);
    ASSERT_FALSE(parsed.diagnostics.empty());
    EXPECT_TRUE(std::ranges::all_of(parsed.diagnostics, [](const auto& diagnostic) {
        return diagnostic.severity == SctDiagnosticSeverity::Warning;
    }));
}

TEST(SctParserDiagnostics, FooterDiagnosticsRetainWarningSeverity) {
    std::vector<std::uint8_t> section;
    appendU32(section, 9u);
    appendU32(section, 0x04000000u);
    appendU32(section, 0x3f800000u);
    appendU32(section, 0x1du);
    section.insert(section.end(), {'T', 'E', 'X', 'T'});
    const auto parsed = SctParser{}.parse(makeSct(std::move(section)), "unterminated_text.sct");

    ASSERT_TRUE(parsed.parseOk);
    ASSERT_TRUE(parsed.file.footer.has_value());
    ASSERT_FALSE(parsed.file.footer->diagnostics.empty());
    EXPECT_EQ(parsed.file.footer->diagnostics.front().severity,
        SctDiagnosticSeverity::Warning);
    EXPECT_TRUE(std::ranges::any_of(parsed.diagnostics, [](const auto& diagnostic) {
        return diagnostic.severity == SctDiagnosticSeverity::Warning
            && diagnostic.message.find("null terminator") != std::string::npos;
    }));
}

TEST(SctParserTrace, ObserverReceivesOrderedPhasesForUncompressedAndCompressedInput) {
    const auto source = cleanSct();
    const auto encoded = spice::compression::aklz::compress(source);
    ASSERT_TRUE(encoded.ok());

    for (const auto& bytes : std::vector<std::vector<std::uint8_t>>{source, encoded.bytes}) {
        std::vector<SctParseTracePhase> phases;
        SctParseOptions options;
        options.traceObserver = [&](const SctParseTraceEvent& event) {
            phases.push_back(event.phase);
        };
        const auto parsed = SctParser{}.parse(bytes, "traced.sct", std::move(options));
        ASSERT_TRUE(parsed.parseOk);
        EXPECT_EQ(phases, (std::vector{
            SctParseTracePhase::Starting,
            SctParseTracePhase::Compression,
            SctParseTracePhase::SectionIndex,
            SctParseTracePhase::InstructionTraversal,
            SctParseTracePhase::Complete,
        }));
    }
}

TEST(SctParserTrace, FailureStopsBeforeComplete) {
    std::vector<SctParseTracePhase> phases;
    SctParseOptions options;
    options.traceObserver = [&](const SctParseTraceEvent& event) {
        phases.push_back(event.phase);
    };
    const auto parsed = SctParser{}.parse({}, "empty.sct", std::move(options));
    ASSERT_FALSE(parsed.parseOk);
    EXPECT_EQ(phases, (std::vector{
        SctParseTracePhase::Starting,
        SctParseTracePhase::Compression,
    }));
}

TEST(SctParserTrace, FileFailureEmitsOneStartingEventAndAnError) {
    std::vector<SctParseTracePhase> phases;
    SctParseOptions options;
    options.traceObserver = [&](const SctParseTraceEvent& event) {
        phases.push_back(event.phase);
    };
    const auto parsed = SctParser{}.parseFile(
        "__spice_sct_v4_missing_input__.sct", std::move(options));
    ASSERT_FALSE(parsed.parseOk);
    EXPECT_EQ(phases, (std::vector{SctParseTracePhase::Starting}));
    ASSERT_FALSE(parsed.diagnostics.empty());
    EXPECT_EQ(parsed.diagnostics.front().severity, SctDiagnosticSeverity::Error);
}

} // namespace
