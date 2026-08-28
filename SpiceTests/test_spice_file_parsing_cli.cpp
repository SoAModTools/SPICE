#include "../SpiceFileParsing/Cli/CliParser.h"
#include "../SpiceFileParsing/Application/OperationPreflight.h"
#include "../SpiceFileParsing/Application/OperationRunner.h"
#include "../Compression/Aklz.h"
#include "../SpiceGvm/SpiceGvm.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <cstdint>
#include <fstream>
#include <iterator>
#include <stop_token>
#include <string_view>
#include <variant>
#include <vector>

namespace {

using spice::fileparsing::cli::ParseDisposition;

spice::fileparsing::cli::ParseResult parse(std::initializer_list<std::string_view> arguments) {
    const std::vector<std::string_view> values(arguments);
    return spice::fileparsing::cli::parse(values);
}

template <typename Request>
void expectRequest(std::initializer_list<std::string_view> arguments) {
    const auto result = parse(arguments);
    ASSERT_EQ(result.disposition, ParseDisposition::Run) << result.text;
    ASSERT_TRUE(result.request.has_value());
    EXPECT_TRUE(std::holds_alternative<Request>(*result.request));
}

void writeBytes(const std::filesystem::path& path, const std::vector<std::uint8_t>& bytes) {
    std::ofstream out(path, std::ios::binary);
    out.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
}

std::vector<std::uint8_t> readBytes(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary);
    return std::vector<std::uint8_t>(std::istreambuf_iterator<char>(in), {});
}

spice::gvm::model::RgbaImage makeRunnerImage(const std::uint8_t seed) {
    spice::gvm::model::RgbaImage image{};
    image.width = 4;
    image.height = 4;
    image.rgba8.resize(4U * 4U * 4U, seed);
    for (std::size_t i = 3; i < image.rgba8.size(); i += 4) {
        image.rgba8[i] = 255;
    }
    return image;
}

TEST(SpiceFileParsingCli, NoArgumentsAndHelpAreSuccessfulDiscoveryPaths) {
    const std::vector<std::string_view> none{};
    const auto noArguments = spice::fileparsing::cli::parse(none);
    EXPECT_EQ(noArguments.disposition, ParseDisposition::Help);
    EXPECT_NE(noArguments.text.find("parse-mld"), std::string::npos);

    const auto globalHelp = parse({ "--help" });
    EXPECT_EQ(globalHelp.disposition, ParseDisposition::Help);

    const auto commandHelp = parse({ "create-gvr", "--help" });
    EXPECT_EQ(commandHelp.disposition, ParseDisposition::Help);
    EXPECT_NE(commandHelp.text.find("--input <png>"), std::string::npos);
}

TEST(SpiceFileParsingCli, ParsesEveryFlatSubcommandToItsTypedRequest) {
    using namespace spice::fileparsing;
    expectRequest<ParseMldRequest>({ "parse-mld", "--input", "in", "--output", "out" });
    expectRequest<CompareMldSa3dRequest>({ "compare-mld-sa3d", "--input", "in", "--output", "out" });
    expectRequest<ExportMldEntryListRequest>({ "export-mld-entry-list", "--input", "in", "--output", "out" });
    expectRequest<InventoryMldGvrFormatsRequest>({ "inventory-mld-gvr-formats", "--input", "in", "--output", "out" });
    expectRequest<ReplaceMldTextureRequest>({ "replace-mld-texture", "--source", "a.mld", "--replacement", "a.png", "--output", "b.mld", "--texture-index", "2" });
    expectRequest<ExtractMldTextureGvrRequest>({ "extract-mld-texture-gvr", "--input", "a.mld", "--output", "a.gvr", "--texture-name", "tk000" });
    expectRequest<ExtractMldTexturePngRequest>({ "extract-mld-texture-png", "--input", "a.mld", "--output", "a.png", "--texture-index", "0" });
    expectRequest<ParseSctRequest>({ "parse-sct", "--input", "in", "--output", "out" });
    expectRequest<ExportSctRequest>({ "export-sct", "--input", "in", "--output", "out" });
    expectRequest<ExportSmlResearchRequest>({ "export-sml-research", "--input", "in", "--output", "out", "--annotation-repository", "annotations", "--command-map" });
    expectRequest<ExportStdJsonRequest>({ "export-std-json", "--input", "in", "--output", "out" });
    expectRequest<ExportMlkCorpusRequest>({ "export-mlk-corpus", "--input", "in", "--output", "out" });
    expectRequest<ExportMlkBlenderIrRequest>({ "export-mlk-blender-ir", "--input", "in", "--output", "out", "--annotation-repository", "annotations" });
    expectRequest<ExportContentGraphRequest>({ "export-content-graph", "--input", "in", "--output", "out" });
    expectRequest<ExportAlxEnemyEventsRequest>({ "export-alx-enemy-events", "--input", "events.csv", "--output", "events.json" });
    expectRequest<CreateGvrRequest>({ "create-gvr", "--input", "a.png", "--output", "a.gvr" });
    expectRequest<ReplaceGvrRequest>({ "replace-gvr", "--source", "a.gvr", "--input", "a.png", "--output", "b.gvr" });
    expectRequest<GvrToPngRequest>({ "gvr-to-png", "--input", "a.gvr", "--output", "a.png" });
    expectRequest<CreateGvrBatchRequest>({ "create-gvr-batch", "--input", "in", "--output", "out" });
    expectRequest<ReplaceGvrBatchRequest>({ "replace-gvr-batch", "--input", "in", "--source-gvr-dir", "sources", "--output", "out" });
    expectRequest<ExportGvrImageIrRequest>({ "export-gvr-image-ir", "--input", "in", "--output", "out" });
    expectRequest<ImportGvrImageIrRequest>({ "import-gvr-image-ir", "--input", "in", "--output", "out" });
    expectRequest<CompressAklzRequest>({ "compress-aklz", "--input", "a.bin", "--output", "a.aklz" });
    expectRequest<DecompressAklzRequest>({ "decompress-aklz", "--input", "a.aklz", "--output", "a.bin" });
}

TEST(SpiceFileParsingCli, MapsDomainOptionsAndSemanticDefaults) {
    using namespace spice::fileparsing;
    const auto createDefaults = parse({ "create-gvr", "--input", "a.png", "--output", "a.gvr" });
    ASSERT_EQ(createDefaults.disposition, ParseDisposition::Run) << createDefaults.text;
    const auto& defaultRequest = std::get<CreateGvrRequest>(*createDefaults.request);
    EXPECT_FALSE(defaultRequest.encoding.format.has_value());
    EXPECT_FALSE(defaultRequest.encoding.paletteFormat.has_value());
    EXPECT_FALSE(defaultRequest.encoding.mipmaps.has_value());
    EXPECT_EQ(defaultRequest.encoding.aklz, AklzPolicy::Raw);
    EXPECT_EQ(defaultRequest.encoding.globalIndex.kind, GvrGlobalIndexKind::None);

    const auto create = parse({
        "create-gvr", "--input", "a.png", "--output", "a.gvr",
        "--format", "ci8", "--palette-format", "rgb565", "--mipmaps", "on",
        "--aklz", "compressed", "--global-index", "0x20"
    });
    ASSERT_EQ(create.disposition, ParseDisposition::Run) << create.text;
    const auto& createRequest = std::get<CreateGvrRequest>(*create.request);
    EXPECT_EQ(createRequest.encoding.format, GvrTextureFormat::CI8);
    EXPECT_EQ(createRequest.encoding.paletteFormat, GvrPaletteFormat::RGB565);
    EXPECT_EQ(createRequest.encoding.mipmaps, true);
    EXPECT_EQ(createRequest.encoding.aklz, AklzPolicy::Compressed);
    EXPECT_EQ(createRequest.encoding.globalIndex.kind, GvrGlobalIndexKind::Value);
    EXPECT_EQ(createRequest.encoding.globalIndex.value, 0x20U);

    const auto replace = parse({
        "replace-gvr", "--source", "a.gvr", "--input", "a.png", "--output", "b.gvr"
    });
    ASSERT_EQ(replace.disposition, ParseDisposition::Run) << replace.text;
    const auto& replaceRequest = std::get<ReplaceGvrRequest>(*replace.request);
    EXPECT_TRUE(replaceRequest.encoding.preserveFormat);
    EXPECT_TRUE(replaceRequest.encoding.preservePaletteFormat);
    EXPECT_TRUE(replaceRequest.encoding.preserveMipmaps);
    EXPECT_EQ(replaceRequest.encoding.aklz, AklzPolicy::Preserve);
    EXPECT_EQ(replaceRequest.encoding.globalIndex.kind, GvrGlobalIndexKind::Preserve);

    const auto sct = parse({
        "export-sct", "--input", "in", "--output", "out",
        "--compression", "aklz", "--decode-unreached-code", "--decompressed-output", "decoded"
    });
    ASSERT_EQ(sct.disposition, ParseDisposition::Run) << sct.text;
    const auto& sctRequest = std::get<ExportSctRequest>(*sct.request);
    EXPECT_TRUE(sctRequest.compressAklz);
    EXPECT_TRUE(sctRequest.decodeUnreachedCode);
    ASSERT_TRUE(sctRequest.paths.decompressedOutput.has_value());
    EXPECT_EQ(*sctRequest.paths.decompressedOutput, std::filesystem::path("decoded"));
}

TEST(SpiceFileParsingCli, RejectsInvalidSelectorsOptionsAndLegacySyntax) {
    EXPECT_EQ(parse({ "replace-mld-texture", "--source", "a.mld", "--replacement", "a.png", "--output", "b.mld" }).disposition, ParseDisposition::Error);
    EXPECT_EQ(parse({ "extract-mld-texture-gvr", "--input", "a.mld", "--output", "a.gvr", "--texture-index", "1", "--texture-name", "tk" }).disposition, ParseDisposition::Error);
    EXPECT_EQ(parse({ "create-gvr", "--input", "a.png", "--output", "a.gvr", "--format", "preserve" }).disposition, ParseDisposition::Error);
    EXPECT_EQ(parse({ "export-sml-research", "--input", "in", "--output", "out", "--annotation-repository", "annotations" }).disposition, ParseDisposition::Error);
    EXPECT_EQ(parse({ "export-sml-research", "--input", "in", "--output", "out", "--annotation-repository", "annotations", "--command-map", "--combined-placement", "raw" }).disposition, ParseDisposition::Error);
    EXPECT_EQ(parse({ "parse-mld", "--input", "in", "--input", "again", "--output", "out" }).disposition, ParseDisposition::Error);
    EXPECT_EQ(parse({ "parse-mld", "in", "out" }).disposition, ParseDisposition::Error);
    EXPECT_EQ(parse({ "--create-gvr", "a.png", "a.gvr" }).disposition, ParseDisposition::Error);
    EXPECT_EQ(parse({ "parse-mld", "--input", "in", "--output", "out", "--sct-only" }).disposition, ParseDisposition::Error);
    EXPECT_EQ(parse({ "parse-mld", "--input", "in", "--output", "out", "--decode-unreached-code" }).disposition, ParseDisposition::Error);
}

TEST(SpiceFileParsingPreflight, RejectsMissingInputWithoutCreatingOutput) {
    const auto tempRoot = std::filesystem::temp_directory_path() / "spice_file_parsing_preflight_test";
    const auto missingInput = tempRoot / "missing_input";
    const auto output = tempRoot / "must_not_be_created";
    std::error_code ec{};
    std::filesystem::remove_all(tempRoot, ec);

    spice::fileparsing::ParseMldRequest request{};
    request.paths.input = missingInput;
    request.paths.output = output;
    const spice::fileparsing::OperationRequest operation = request;

    const auto validation = spice::fileparsing::preflight::validate(operation);
    ASSERT_TRUE(validation.has_value());
    EXPECT_NE(validation->find("input directory not found"), std::string::npos);
    EXPECT_FALSE(std::filesystem::exists(output));
}

TEST(SpiceFileParsingPreflight, RequiresExistingAnnotationRepository) {
    const auto tempRoot = std::filesystem::temp_directory_path() / "spice_file_parsing_annotation_preflight_test";
    const auto input = tempRoot / "input";
    const auto missingAnnotations = tempRoot / "missing_annotations";
    std::error_code ec{};
    std::filesystem::remove_all(tempRoot, ec);
    ASSERT_TRUE(std::filesystem::create_directories(input, ec));

    spice::fileparsing::ExportSmlResearchRequest request{};
    request.input = input;
    request.output = tempRoot / "output";
    request.annotationRepository = missingAnnotations;
    request.commandMap = true;
    const spice::fileparsing::OperationRequest operation = request;

    const auto validation = spice::fileparsing::preflight::validate(operation);
    ASSERT_TRUE(validation.has_value());
    EXPECT_NE(validation->find("annotation repository not found"), std::string::npos);
    EXPECT_FALSE(std::filesystem::exists(request.output));
    std::filesystem::remove_all(tempRoot, ec);
}

TEST(SpiceFileParsingPreflight, ValidatesEveryBatchReplacementSourceBeforeCreatingOutput) {
    const auto tempRoot = std::filesystem::temp_directory_path() / "spice_file_parsing_batch_preflight_test";
    const auto input = tempRoot / "input";
    const auto sources = tempRoot / "sources";
    const auto output = tempRoot / "output";
    std::error_code ec{};
    std::filesystem::remove_all(tempRoot, ec);
    ASSERT_TRUE(std::filesystem::create_directories(input, ec));
    ASSERT_TRUE(std::filesystem::create_directories(sources, ec));
    writeBytes(input / "missing.PNG", { 1U });

    spice::fileparsing::ReplaceGvrBatchRequest request{};
    request.input = input;
    request.sourceGvrDirectory = sources;
    request.output = output;
    const auto validation = spice::fileparsing::preflight::validate(request);

    ASSERT_TRUE(validation.has_value());
    EXPECT_NE(validation->find("source GVR not found"), std::string::npos);
    EXPECT_FALSE(std::filesystem::exists(output));
    std::filesystem::remove_all(tempRoot, ec);
}

TEST(SpiceFileParsingRunner, PreflightFailureReportsStructuredErrorBeforeWriting) {
    const auto tempRoot = std::filesystem::temp_directory_path() / "spice_file_parsing_runner_preflight_test";
    std::error_code ec{};
    std::filesystem::remove_all(tempRoot, ec);

    spice::fileparsing::ParseMldRequest request{};
    request.paths.input = tempRoot / "missing";
    request.paths.output = tempRoot / "output";
    std::vector<spice::fileparsing::OperationEvent> events{};
    spice::fileparsing::OperationContext context{};
    context.report = [&](const auto& event) { events.push_back(event); };

    const auto result = spice::fileparsing::OperationRunner{}.run(request, context);

    EXPECT_EQ(result.status, spice::fileparsing::OperationStatus::Failure);
    EXPECT_FALSE(std::filesystem::exists(request.paths.output));
    ASSERT_FALSE(events.empty());
    EXPECT_EQ(events.front().level, spice::fileparsing::EventLevel::Error);
}

TEST(SpiceFileParsingRunner, WritesDecompressedCopyOnlyAtExplicitLocation) {
    const auto tempRoot = std::filesystem::temp_directory_path() / "spice_file_parsing_runner_decompression_test";
    const auto input = tempRoot / "input";
    const auto output = tempRoot / "output";
    const auto decompressed = tempRoot / "decompressed";
    std::error_code ec{};
    std::filesystem::remove_all(tempRoot, ec);
    ASSERT_TRUE(std::filesystem::create_directories(input, ec));

    const std::vector<std::uint8_t> raw(64U, 0U);
    const auto compressed = spice::compression::aklz::compress(raw);
    ASSERT_TRUE(compressed.ok());
    writeBytes(input / "sample.sct", compressed.bytes);

    spice::fileparsing::ParseSctRequest request{};
    request.paths.input = input;
    request.paths.output = output;
    request.paths.decompressedOutput = decompressed;
    std::vector<spice::fileparsing::OperationEvent> events{};
    spice::fileparsing::OperationContext context{};
    context.report = [&](const auto& event) { events.push_back(event); };

    const auto result = spice::fileparsing::OperationRunner{}.run(request, context);

    EXPECT_EQ(result.status, spice::fileparsing::OperationStatus::Success);
    EXPECT_EQ(readBytes(decompressed / "sample.sct"), raw);
    EXPECT_FALSE(std::filesystem::exists(output / "sample.sct"));
    EXPECT_TRUE(std::any_of(events.begin(), events.end(), [](const auto& event) {
        return event.level == spice::fileparsing::EventLevel::Progress;
    }));
    std::filesystem::remove_all(tempRoot, ec);
}

TEST(SpiceFileParsingRunner, DeliversWarningsAsStructuredEvents) {
    const auto tempRoot = std::filesystem::temp_directory_path() / "spice_file_parsing_runner_warning_test";
    const auto input = tempRoot / "input";
    std::error_code ec{};
    std::filesystem::remove_all(tempRoot, ec);
    ASSERT_TRUE(std::filesystem::create_directories(input, ec));
    writeBytes(input / "invalid.gvr.json", { 1U, 2U, 3U, 4U });

    spice::fileparsing::ImportGvrImageIrRequest request{};
    request.input = input;
    request.output = tempRoot / "output";
    std::vector<spice::fileparsing::OperationEvent> events{};
    spice::fileparsing::OperationContext context{};
    context.report = [&](const auto& event) { events.push_back(event); };

    const auto result = spice::fileparsing::OperationRunner{}.run(request, context);

    EXPECT_EQ(result.status, spice::fileparsing::OperationStatus::Success);
    EXPECT_TRUE(std::any_of(events.begin(), events.end(), [](const auto& event) {
        return event.level == spice::fileparsing::EventLevel::Warning;
    }));
    std::filesystem::remove_all(tempRoot, ec);
}

TEST(SpiceFileParsingRunner, CancellationStopsBetweenDirectoryEntries) {
    const auto tempRoot = std::filesystem::temp_directory_path() / "spice_file_parsing_runner_cancellation_test";
    const auto input = tempRoot / "input";
    const auto output = tempRoot / "output";
    std::error_code ec{};
    std::filesystem::remove_all(tempRoot, ec);
    ASSERT_TRUE(std::filesystem::create_directories(input, ec));
    spice::gvm::image::writePngRgba8(input / "first.png", makeRunnerImage(25U));
    spice::gvm::image::writePngRgba8(input / "second.png", makeRunnerImage(75U));

    spice::fileparsing::CreateGvrBatchRequest request{};
    request.input = input;
    request.output = output;
    std::stop_source stopSource{};
    spice::fileparsing::OperationContext context{};
    context.stopToken = stopSource.get_token();
    context.report = [&](const auto& event) {
        if (event.message.find("Creating GVR:") != std::string::npos) {
            stopSource.request_stop();
        }
    };

    const auto result = spice::fileparsing::OperationRunner{}.run(request, context);

    EXPECT_EQ(result.status, spice::fileparsing::OperationStatus::Cancelled);
    EXPECT_EQ(result.filesProcessed, 1U);
    std::size_t gvrCount = 0;
    for (const auto& entry : std::filesystem::directory_iterator(output)) {
        if (entry.path().extension() == ".gvr") {
            ++gvrCount;
        }
    }
    EXPECT_EQ(gvrCount, 1U);
    std::filesystem::remove_all(tempRoot, ec);
}

} // namespace
