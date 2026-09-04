#include "../../SpiceMLD/SpiceMLD.h"
#include "../../SpiceMlk/SpiceMlk.h"
#include "../../SpiceSstSml/SpiceSstSml.h"
#include "../../SpiceSCT/SpiceSCT.h"
#include "../../SpiceStd/SpiceStd.h"
#include "../../SpiceContentGraph/SpiceContentGraph.h"
#include "../../SpiceGvm/SpiceGvm.h"
#include "../../Compression/Aklz.h"
#include "../../Sa3Dport/Testing/Slice2TestApi.h"
#include "../../Sa3Dport/Testing/Slice5TestApi.h"
#include "../../Sa3Dport/Testing/Slice6TestApi.h"
#include "../../Sa3Dport/Testing/Slice7TestApi.h"
#include "../../Sa3Dport/Testing/Slice8TestApi.h"
#include "../../Sa3Dport/Testing/Slice9TestApi.h"
#include "../AlxEnemyEventExport.h"
#include "OperationExecution.h"
#include "DreamcastParityAudit.h"

#include <algorithm>
#include <cctype>
#include <cstddef>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <map>
#include <optional>
#include <span>
#include <sstream>
#include <string_view>
#include <string>
#include <stdexcept>
#include <system_error>
#include <utility>
#include <vector>

namespace {

template <typename... Values>
void emit(
    spice::mix::OperationContext& context,
    const spice::mix::EventLevel level,
    Values&&... values) {
    if (!context.report) {
        return;
    }
    std::ostringstream message{};
    (message << ... << std::forward<Values>(values));
    context.report({ level, message.str() });
}

std::vector<std::uint8_t> readAllBytes(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        return {};
    }

    in.seekg(0, std::ios::end);
    const auto size = in.tellg();
    in.seekg(0, std::ios::beg);
    if (size <= 0) {
        return {};
    }

    std::vector<std::uint8_t> bytes(static_cast<std::size_t>(size));
    in.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    return bytes;
}

bool writeAllBytes(const std::filesystem::path& path, std::span<const std::uint8_t> bytes) {
    std::ofstream out(path, std::ios::binary);
    if (!out) {
        return false;
    }

    out.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    return out.good();
}

bool writeAllBytesCreatingParents(const std::filesystem::path& path, std::span<const std::uint8_t> bytes) {
    if (path.has_parent_path()) {
        std::filesystem::create_directories(path.parent_path());
    }
    return writeAllBytes(path, bytes);
}

#include "SctSmlOperations.inl"
enum class DirectoryOperationKind {
    ParseMld,
    CompareMldSa3d,
    ExportMldEntryList,
    InventoryMldGvrFormats,
    ParseSct,
    ExportSct,
    ExportSmlResearch,
    ExportStdJson,
    ExportContentGraph,
    CreateGvrBatch,
    ReplaceGvrBatch,
    ExportGvrImageIr,
    ImportGvrImageIr,
};

struct DirectoryOperation {
    DirectoryOperationKind kind = DirectoryOperationKind::ParseMld;
    spice::mix::DirectoryPaths paths{};
    bool extractGrndGobjBlocks = false;
    bool decodeSctUnreachedCode = false;
    bool compressSctAklz = false;
    std::filesystem::path annotationRepository{};
    bool embeddedMld = false;
    bool embeddedMldBlenderIr = false;
    bool combinedBlenderIr = false;
    bool combinedRawPlacement = false;
    bool commandMap = false;
    std::filesystem::path sourceGvrDirectory{};
    spice::mix::GvrEncodingSettings encoding{};
    spice::mix::AklzPolicy importAklz = spice::mix::AklzPolicy::Preserve;
    spice::mix::ContentGraphProjection contentGraphProjection =
        spice::mix::ContentGraphProjection::Full;
};

spice::gvm::ir::AklzPolicy toRuntimeAklzPolicy(spice::mix::AklzPolicy policy);
spice::gvm::model::TextureFormat toRuntimeTextureFormat(spice::mix::GvrTextureFormat format);
spice::gvm::model::PaletteFormat toRuntimePaletteFormat(spice::mix::GvrPaletteFormat format);

std::string quotePath(const std::filesystem::path& path) {
    std::string value = path.string();
    std::string escaped{};
    escaped.reserve(value.size() + 2);
    escaped.push_back('"');
    for (const char c : value) {
        if (c == '"') {
            escaped.push_back('\\');
        }
        escaped.push_back(c);
    }
    escaped.push_back('"');
    return escaped;
}

std::string jsonEscape(std::string value) {
    std::string escaped{};
    escaped.reserve(value.size() + 8);
    for (const char c : value) {
        switch (c) {
        case '\\':
            escaped += "\\\\";
            break;
        case '"':
            escaped += "\\\"";
            break;
        case '\n':
            escaped += "\\n";
            break;
        case '\r':
            escaped += "\\r";
            break;
        case '\t':
            escaped += "\\t";
            break;
        default:
            escaped.push_back(c);
            break;
        }
    }
    return escaped;
}

bool endsWithInsensitive(std::string value, std::string suffix) {
    value = toLowerCopy(std::move(value));
    suffix = toLowerCopy(std::move(suffix));
    return value.size() >= suffix.size()
        && value.compare(value.size() - suffix.size(), suffix.size(), suffix) == 0;
}

#include "GvrOperations.inl"
#include "MldOperations.inl"
#include "AklzAlxOperations.inl"



} // namespace

namespace {

int executeDirectoryOperation(
    const DirectoryOperation& operation,
    spice::mix::OperationContext& context) {
    const std::filesystem::path processDir = context.executableDirectory;
    const std::filesystem::path inputDir = operation.paths.input;
    const std::filesystem::path outputDir = operation.paths.output;

    if (!std::filesystem::exists(inputDir) || !std::filesystem::is_directory(inputDir)) {
        emit(context, spice::mix::EventLevel::Error, "Input directory not found: ", inputDir);
        return 1;
    }
    if (operation.kind == DirectoryOperationKind::ReplaceGvrBatch
        && (!std::filesystem::exists(operation.sourceGvrDirectory)
            || !std::filesystem::is_directory(operation.sourceGvrDirectory))) {
        emit(context, spice::mix::EventLevel::Error,
            "Replacement source GVR directory not found: ", operation.sourceGvrDirectory);
        return 1;
    }

    std::filesystem::create_directories(outputDir);
    if (operation.paths.decompressedOutput.has_value()) {
        std::filesystem::create_directories(*operation.paths.decompressedOutput);
    }
    if (operation.kind == DirectoryOperationKind::CompareMldSa3d) {
        writeFixtureManifestFromInputDir(inputDir, outputDir);
    }
    emit(context, spice::mix::EventLevel::Progress,
        "Step 2/4: Prepared directories.");

    spice::sct::SctParser sctParser{};
    spice::mld::parsing::MldParser mldParser{};
    spice::mld::exporting::BlenderIrJsonExporter exporter{};
    emit(context, spice::mix::EventLevel::Progress,
        "Step 3/4: Parsing input files...");

    std::size_t filesProcessed = 0;
    constexpr int kAbStartSlice = 1;
    constexpr int kAbEndSlice = 9;
    spice::contentgraph::ContentGraphCorpusInput contentGraphInput{};
    spice::mld::analysis::MldGvrFormatInventoryBuilder mldGvrFormatInventoryBuilder{};

    if (operation.kind == DirectoryOperationKind::CreateGvrBatch
        || operation.kind == DirectoryOperationKind::ReplaceGvrBatch) {
        try {
            filesProcessed = operation.kind == DirectoryOperationKind::CreateGvrBatch
                ? createGvrBatch(operation, context)
                : replaceGvrBatch(operation, context);
        } catch (const std::exception& ex) {
            emit(context, spice::mix::EventLevel::Error,
                "ERROR: ", ex.what());
            return 1;
        }
        emit(context, spice::mix::EventLevel::Progress,
            "Step 4/4: Finalizing summary.");
        emit(context, spice::mix::EventLevel::Info, "Operation finished.");
        emit(context, spice::mix::EventLevel::Info, "FilesProcessed=", filesProcessed);
        emit(context, spice::mix::EventLevel::Info, "inputDir=", inputDir);
        emit(context, spice::mix::EventLevel::Info, "outputDir=", outputDir);
        return 0;
    }

    for (const auto& entry : std::filesystem::directory_iterator(inputDir)) {
        if (context.stopToken.stop_requested()) {
            break;
        }
        if (!entry.is_regular_file()) {
            continue;
        }

        const auto extension = toLowerCopy(entry.path().extension().string());
        const auto bytes = readAllBytes(entry.path());
        if (bytes.empty() && operation.kind != DirectoryOperationKind::ImportGvrImageIr) {
            continue;
        }

        if (operation.kind == DirectoryOperationKind::ExportGvrImageIr) {
            if (extension != ".gvr") {
                continue;
            }
            emit(context, spice::mix::EventLevel::Progress,
                "  - Exporting GVR image IR: ", entry.path().filename().string());
            try {
                const auto exported = spice::gvm::ir::exportGvrImageIr(
                    std::span<const std::uint8_t>(bytes.data(), bytes.size()),
                    entry.path(),
                    outputDir);
                const auto reportPath = outputDir / (entry.path().stem().string() + ".gvr.ir.txt");
                std::ofstream reportOut(reportPath, std::ios::binary);
                reportOut << "source=" << entry.path().string() << "\n";
                reportOut << "png=" << exported.pngPath.string() << "\n";
                reportOut << "json=" << exported.jsonPath.string() << "\n";
                for (const auto& diagnostic : exported.diagnostics) {
                    reportOut << "diagnostic=" << diagnostic << "\n";
                }
                ++filesProcessed;
            } catch (const std::exception& ex) {
                emit(context, spice::mix::EventLevel::Warning,
                    "WARNING: GVR image IR export failed for ",
                    entry.path().string(), ": ", ex.what());
            }
            continue;
        }

        if (operation.kind == DirectoryOperationKind::ImportGvrImageIr) {
            if (!endsWithInsensitive(entry.path().filename().string(), ".gvr.json")) {
                continue;
            }
            emit(context, spice::mix::EventLevel::Progress,
                "  - Importing GVR image IR: ", entry.path().filename().string());
            try {
                const auto aklzPolicy = toRuntimeAklzPolicy(operation.importAklz);
                const auto imported = spice::gvm::ir::importGvrImageIr(entry.path(), aklzPolicy);
                const auto outPath = outputDir / (gvrSidecarStem(entry.path()) + ".imported.gvr");
                if (!writeAllBytes(outPath, std::span<const std::uint8_t>(imported.bytes.data(), imported.bytes.size()))) {
                    emit(context, spice::mix::EventLevel::Warning,
                        "WARNING: failed to write imported GVR: ", outPath.string());
                }
                const auto reportPath = outputDir / (gvrSidecarStem(entry.path()) + ".gvr.import.txt");
                std::ofstream reportOut(reportPath, std::ios::binary);
                reportOut << "source=" << entry.path().string() << "\n";
                reportOut << "output=" << outPath.string() << "\n";
                reportOut << "aklzPolicy=" << spice::gvm::ir::to_string(aklzPolicy) << "\n";
                for (const auto& diagnostic : imported.diagnostics) {
                    reportOut << "diagnostic=" << diagnostic << "\n";
                }
                ++filesProcessed;
            } catch (const std::exception& ex) {
                emit(context, spice::mix::EventLevel::Warning,
                    "WARNING: GVR image IR import failed for ",
                    entry.path().string(), ": ", ex.what());
            }
            continue;
        }

        if (operation.kind == DirectoryOperationKind::ExportStdJson) {
            if (extension != ".std") {
                continue;
            }
            emit(context, spice::mix::EventLevel::Progress,
                "  - Exporting STD JSON: ", entry.path().filename().string());
            try {
                const auto parsed = spice::stdfile::parseFile(entry.path());
                const auto jsonOutPath = outputDir / (entry.path().stem().string() + ".std.json");
                std::ofstream jsonOut(jsonOutPath, std::ios::binary);
                jsonOut << spice::stdfile::StdJsonExporter{}.toJson(parsed);
                if (!jsonOut.good()) {
                    emit(context, spice::mix::EventLevel::Warning,
                        "WARNING: failed to write STD JSON: ", jsonOutPath.string());
                } else {
                    ++filesProcessed;
                }
            } catch (const std::exception& ex) {
                emit(context, spice::mix::EventLevel::Warning,
                    "WARNING: STD JSON export failed for ",
                    entry.path().string(), ": ", ex.what());
            }
            continue;
        }

        if (operation.kind == DirectoryOperationKind::ExportSmlResearch) {
            if (extension != ".sml") {
                continue;
            }

            emit(context, spice::mix::EventLevel::Progress,
                "  - Exporting SML research artifacts: ",
                entry.path().filename().string());
            const auto stem = entry.path().stem().string();
            const auto stageOutputDir = outputDir / stem;

            try {
                const auto imported = spice::sstsml::SstSmlDocumentImporter::importFile(entry.path());
                if (!imported.ok()) {
                    for (const auto& diagnostic : imported.diagnostics) {
                        emit(context, spice::mix::EventLevel::Warning,
                            "WARNING: SST/SML import: ", diagnostic.message);
                    }
                    continue;
                }
                const auto& document = *imported.document;
                const auto analysis = spice::sstsml::SstSmlDocumentAnalyzer::analyze(
                    document, imported.receipt);
                if (!analysis.ok()) {
                    emit(context, spice::mix::EventLevel::Warning,
                        "WARNING: SST/SML analysis failed for ", entry.path().string());
                    continue;
                }

                std::map<std::size_t, std::filesystem::path> blenderIrPaths{};
                std::map<std::size_t, spice::sstsml::SmlBlenderIrEntrySummary> blenderIrSummaries{};
                std::optional<std::filesystem::path> combinedBlenderIrPath{};
                std::optional<spice::sstsml::exporting::SmlBlenderIrCombiner> combinedBlenderIr{};
                if (operation.combinedBlenderIr) {
                    combinedBlenderIr.emplace();
                }

                if (operation.embeddedMldBlenderIr || operation.combinedBlenderIr) {
                    for (std::size_t recordIndex = 0U; recordIndex < document.members.size(); ++recordIndex) {
                        if (context.stopToken.stop_requested()) {
                            break;
                        }
                        const auto& embeddedMldBytes = document.members[recordIndex].sml.resource.bytes;
                        if (embeddedMldBytes.empty()) {
                            continue;
                        }

                        const auto blenderIrDir = stageOutputDir / "blender_ir" /
                            ("entry_" + std::to_string(recordIndex));
                        spice::mld::parsing::ParseOptions embeddedMldOptions{};
                        embeddedMldOptions.buildBlenderIntermediateIr = true;
                        embeddedMldOptions.exportBlenderIrJson = false;
                        embeddedMldOptions.blenderIrOutputDir = blenderIrDir.string();
                        try {
                            auto parsedEmbeddedMld = mldParser.parse(
                                std::span<const std::uint8_t>(
                                    embeddedMldBytes.data(),
                                    embeddedMldBytes.size()),
                                embeddedMldOptions);
                            if (parsedEmbeddedMld.blenderIrScene.has_value()) {
                                blenderIrSummaries[recordIndex] = summarizeSmlEntryBlenderIr(*parsedEmbeddedMld.blenderIrScene);
                                if (combinedBlenderIr.has_value()) {
                                    const auto sstPlacementOverlay = operation.combinedRawPlacement
                                        ? std::optional<spice::sstsml::exporting::SmlBlenderIrSstPlacementOverlay>{}
                                        : sstType0PlacementOverlayForRecord(document, recordIndex);
                                    if (operation.embeddedMldBlenderIr) {
                                        combinedBlenderIr->appendEntryScene(
                                            *parsedEmbeddedMld.blenderIrScene,
                                            stem,
                                            recordIndex,
                                            sstPlacementOverlay);
                                    } else {
                                        combinedBlenderIr->appendEntryScene(
                                            std::move(*parsedEmbeddedMld.blenderIrScene),
                                            stem,
                                            recordIndex,
                                            sstPlacementOverlay);
                                    }
                                }

                                if (operation.embeddedMldBlenderIr) {
                                    const auto blenderIrPath = blenderIrDir / "blender_ir_scene.json";
                                    std::filesystem::create_directories(blenderIrDir);
                                    spice::sstsml::exporting::namespaceSmlEntryBlenderIrScene(
                                        *parsedEmbeddedMld.blenderIrScene,
                                        stem,
                                        recordIndex);
                                    std::ofstream blenderIrOut(blenderIrPath, std::ios::binary);
                                    blenderIrOut << exporter.toJson(*parsedEmbeddedMld.blenderIrScene);
                                    if (!blenderIrOut.good()) {
                                        emit(context, spice::mix::EventLevel::Warning,
                                            "WARNING: failed to write embedded MLD Blender IR for ",
                                            entry.path().filename().string(), " record ", recordIndex);
                                        continue;
                                    }
                                    blenderIrPaths[recordIndex] = blenderIrPath;
                                }
                            } else {
                                emit(context, spice::mix::EventLevel::Warning,
                                    "WARNING: embedded MLD Blender IR was not produced for ",
                                    entry.path().filename().string(), " record ", recordIndex);
                            }
                        } catch (const std::exception& ex) {
                            emit(context, spice::mix::EventLevel::Warning,
                                "WARNING: embedded MLD parse failed for ",
                                entry.path().filename().string(), " record ", recordIndex, ": ", ex.what());
                        }
                    }

                    if (combinedBlenderIr.has_value()) {
                        const auto combinedPath = stageOutputDir / (stem + "_combined_blender_ir_scene.json");
                        std::filesystem::create_directories(stageOutputDir);
                        std::ofstream combinedOut(combinedPath, std::ios::binary);
                        combinedOut << exporter.toJson(combinedBlenderIr->scene());
                        if (!combinedOut.good()) {
                            emit(context, spice::mix::EventLevel::Warning,
                                "WARNING: failed to write combined SML Blender IR for ",
                                entry.path().filename().string());
                        } else {
                            combinedBlenderIrPath = combinedPath;
                        }
                    }
                }

                spice::sstsml::SmlEmbeddedMldExportOptions exportOptions{};
                exportOptions.stageOutputDir = stageOutputDir;
                exportOptions.stageAnnotationRepositoryDir = operation.annotationRepository;
                exportOptions.stem = stem;
                exportOptions.writeEmbeddedMldPayloads = operation.embeddedMld;
                exportOptions.writeCommandMap = operation.commandMap;
                exportOptions.combinedBlenderIrPath = combinedBlenderIrPath;
                exportOptions.blenderIrPathsByRecordIndex = std::move(blenderIrPaths);
                exportOptions.blenderIrSummariesByRecordIndex = std::move(blenderIrSummaries);
                const auto exportResult = spice::sstsml::exportSmlEmbeddedMldsAndCommandMap(
                    document,
                    imported.receipt,
                    analysis,
                    exportOptions);

                if (!exportResult.wroteManifest) {
                    emit(context, spice::mix::EventLevel::Warning,
                        "WARNING: failed to write SML embedded MLD manifest for ",
                        entry.path().string());
                }
                if (operation.commandMap && !exportResult.wroteCommandMap) {
                    emit(context, spice::mix::EventLevel::Warning,
                        "WARNING: no SST/SML command map was written for ",
                        entry.path().string());
                }
                if (!exportResult.wroteStageAnnotationTemplate) {
                    emit(context, spice::mix::EventLevel::Warning,
                        "WARNING: no SST/SML stage annotation template was written for ",
                        entry.path().string());
                }
                ++filesProcessed;
            } catch (const std::exception& ex) {
                emit(context, spice::mix::EventLevel::Warning,
                    "WARNING: SML embedded MLD export failed for ",
                    entry.path().string(), ": ", ex.what());
            }
            continue;
        }

        if ((operation.kind == DirectoryOperationKind::CompareMldSa3d
                || operation.kind == DirectoryOperationKind::ExportMldEntryList
                || operation.kind == DirectoryOperationKind::InventoryMldGvrFormats)
            && extension != ".mld") {
            continue;
        }
        if (operation.kind == DirectoryOperationKind::ParseMld && extension != ".mld") {
            continue;
        }
        if (operation.kind == DirectoryOperationKind::ParseSct && extension != ".sct") {
            continue;
        }
        if (operation.kind == DirectoryOperationKind::ExportSct && extension != ".sct") {
            continue;
        }

        const bool isSupportedExtension = extension == ".sct" || extension == ".mld";
        if (operation.paths.decompressedOutput.has_value()
            && isSupportedExtension
            && spice::compression::aklz::isAklz(bytes)) {
            auto decodedResult = spice::compression::aklz::decompress(bytes);
            if (!decodedResult.ok()) {
                emit(context, spice::mix::EventLevel::Warning,
                    "AKLZ decompression failed for ", entry.path().string(), ": ",
                    spice::compression::aklz::errorToString(decodedResult.error));
            } else {
                const auto decompressedPath = *operation.paths.decompressedOutput / entry.path().filename();
                if (!writeAllBytes(decompressedPath, std::span<const std::uint8_t>(decodedResult.bytes.data(), decodedResult.bytes.size()))) {
                    emit(context, spice::mix::EventLevel::Warning,
                        "Failed to write decompressed file: ", decompressedPath.string());
                }
            }
        }

        if (extension == ".sct") {
            emit(context, spice::mix::EventLevel::Progress,
                "  - Parsing SCT: ", entry.path().filename().string());
            spice::sct::SctParseOptions sctParseOptions{};
            sctParseOptions.decodeUnreachedCode = operation.decodeSctUnreachedCode;
            auto parsed = sctParser.parse(
                std::span<const std::uint8_t>(bytes.data(), bytes.size()),
                entry.path().string(),
                sctParseOptions);
            auto sctIr = spice::sct::SctIrBuilder{}.build(parsed);
            if (operation.kind == DirectoryOperationKind::ExportContentGraph) {
                contentGraphInput.sctFiles.push_back({entry.path().string(), std::move(sctIr)});
                ++filesProcessed;
                continue;
            }
            const auto outPath = outputDir / (entry.path().stem().string() + ".sct.txt");
            std::string summary = spice::sct::formatParseSummary(parsed);
            std::ofstream out(outPath, std::ios::binary);
            out << summary.c_str();

            const auto jsonOutPath = outputDir / (entry.path().stem().string() + ".sct.json");
            std::ofstream jsonOut(jsonOutPath, std::ios::binary);
            jsonOut << spice::sct::SctJsonExporter{}.toJson(sctIr);

            if (operation.kind == DirectoryOperationKind::ExportSct) {
                spice::sct::SctExportOptions exportOptions{};
                exportOptions.compressAklz = operation.compressSctAklz;
                const auto exportedSct = spice::sct::SctBinaryExporter{}.exportFile(sctIr, exportOptions);
                const auto exportedSctPath = outputDir / (entry.path().stem().string()
                    + (operation.compressSctAklz ? ".canonical.aklz.sct" : ".canonical.sct"));
                if (!writeAllBytes(exportedSctPath, std::span<const std::uint8_t>(exportedSct.data(), exportedSct.size()))) {
                    emit(context, spice::mix::EventLevel::Warning,
                        "WARNING: failed to write SCT binary export: ",
                        exportedSctPath.string());
                }

                const auto reparsed = sctParser.parse(
                    std::span<const std::uint8_t>(exportedSct.data(), exportedSct.size()),
                    exportedSctPath.string());
                const auto comparison = spice::sct::SctSemanticComparer{}.compare(sctIr, reparsed);
                const auto validationPath = outputDir / (entry.path().stem().string() + ".sct.roundtrip.txt");
                std::ofstream validationOut(validationPath, std::ios::binary);
                validationOut << "source=" << entry.path().string() << "\n";
                validationOut << "export=" << exportedSctPath.string() << "\n";
                validationOut << "reparseOk=" << (reparsed.parseOk ? "true" : "false") << "\n";
                validationOut << "semanticEquivalent=" << (comparison.equivalent ? "true" : "false") << "\n";
                for (const auto& difference : comparison.differences) {
                    validationOut << "difference=" << difference << "\n";
                }
                if (!comparison.equivalent) {
                    emit(context, spice::mix::EventLevel::Warning,
                        "WARNING: SCT canonical export semantic comparison failed for ",
                        entry.path().string());
                }
            }
            ++filesProcessed;
            continue;
        }

        if (extension == ".mld") {
            emit(context, spice::mix::EventLevel::Progress,
                "  - Parsing MLD: ", entry.path().filename().string());
            auto mldFile = mldParser.parseBytes(std::span<const std::uint8_t>(bytes.data(), bytes.size()));
            if (operation.kind == DirectoryOperationKind::InventoryMldGvrFormats) {
                mldGvrFormatInventoryBuilder.noteFileScanned();
                try {
                    if (mldFile.textureArchive.has_value()) {
                        mldGvrFormatInventoryBuilder.addParsedMld(entry.path().string(), *mldFile.textureArchive);
                    } else {
                        mldGvrFormatInventoryBuilder.addParseFailure(entry.path().string(), "No texture archive was parsed.");
                    }
                    ++filesProcessed;
                } catch (const std::exception& ex) {
                    mldGvrFormatInventoryBuilder.addParseFailure(entry.path().string(), ex.what());
                    emit(context, spice::mix::EventLevel::Warning,
                        "WARNING: MLD GVR format sampling failed for ",
                        entry.path().string(), ": ", ex.what());
                }
                continue;
            }
            if (operation.kind == DirectoryOperationKind::ExportContentGraph) {
                contentGraphInput.mldFiles.push_back({entry.path().string(), std::move(mldFile)});
                ++filesProcessed;
                continue;
            }
            if (operation.kind == DirectoryOperationKind::ExportMldEntryList) {
                spice::mld::parsing::ParseOptions entryListOptions{};
                entryListOptions.entryListOnly = true;
                entryListOptions.buildBlenderIntermediateIr = false;
                auto entryListParsed = mldParser.project(mldFile, entryListOptions);

                const auto entryListOutPath = outputDir / (entry.path().stem().string() + ".mld.entries.json");
                std::ofstream entryListOut(entryListOutPath, std::ios::binary);
                entryListOut << spice::mld::exporting::MldEntryListJsonExporter{}.toJson(
                    entry.path(), entryListParsed.entryList);
                if (!entryListOut) {
                    throw std::runtime_error("failed to write MLD entry list: " + entryListOutPath.string());
                }
                ++filesProcessed;
                continue;
            }

            if (operation.kind == DirectoryOperationKind::CompareMldSa3d) {
                spice::mld::parsing::ParseOptions sa3dPortOptions{};
                sa3dPortOptions.extractGrndGobjBlocks = operation.extractGrndGobjBlocks;
                auto sa3dPortParsed = mldParser.project(mldFile, sa3dPortOptions);

                const auto sa3dPortOutPath = outputDir / (entry.path().stem().string() + ".mld.sa3d_port.txt");
                std::ofstream sa3dPortOut(sa3dPortOutPath, std::ios::binary);
                sa3dPortOut << spice::mld::parsing::formatParseSummary(sa3dPortParsed);
                if (operation.extractGrndGobjBlocks) {
                    writeExtractedSpatialBlocks(context, outputDir, entry.path().stem().string(), sa3dPortParsed.extractedSpatialBlocks);
                }

                const auto jsonOutPath = outputDir / (entry.path().stem().string() + ".sa3d_port.json");
                std::ofstream jsonOut(jsonOutPath, std::ios::binary);
                if (sa3dPortParsed.blenderIrScene.has_value()) {
                    jsonOut << exporter.toJson(*sa3dPortParsed.blenderIrScene).c_str();
                }

                std::vector<std::filesystem::path> bridgeReportPaths{};
                std::vector<std::filesystem::path> blockInputPaths{};
                std::vector<spice::mld::parsing::ExtractedNjBlock> validBlocks{};
                for (const auto& block : sa3dPortParsed.extractedNjBlocks) {
                    const auto normalizedBlock = normalizeBlockForSa3dBridge(block);
                    if (!normalizedBlock.has_value()) {
                        continue;
                    }

                    const auto kindLabel = toBlockKindLabel(normalizedBlock->kind);
                    const auto pairLabel = normalizedBlock->includesNjtlPrefix ? "_njtl_njcm" : "";
                    const auto blockStem = entry.path().stem().string() + ".block_" + std::to_string(normalizedBlock->offset) + "_" + kindLabel + pairLabel;
                    const auto blockInputPath = outputDir / (blockStem + ".njblk.bin");
                    if (!writeAllBytes(blockInputPath, std::span<const std::uint8_t>(normalizedBlock->bytes.data(), normalizedBlock->bytes.size()))) {
                        emit(context, spice::mix::EventLevel::Warning,
                            "WARNING: failed to write extracted NJ block input: ",
                            blockInputPath.string());
                        continue;
                    }
                    blockInputPaths.push_back(blockInputPath);
                    validBlocks.push_back(*normalizedBlock);
                }

                const auto blockManifestPath = outputDir / (entry.path().stem().string() + ".block_manifest.json");
                writeFixtureBlockManifest(blockManifestPath, entry.path().stem().string(), blockInputPaths, validBlocks);

                for (int slice = kAbStartSlice; slice <= kAbEndSlice; ++slice) {
                    if (context.stopToken.stop_requested()) {
                        break;
                    }
                    const auto bridgeReportPath = maybeInvokeDotnetBridge(
                        context,
                        processDir,
                        entry.path(),
                        outputDir,
                        outputDir / "FIXTURE_MANIFEST.generated.json",
                        blockManifestPath,
                        slice);
                    if (bridgeReportPath.has_value()) {
                        bridgeReportPaths.push_back(*bridgeReportPath);
                    }
                }

                const auto compareOutPath = outputDir / (entry.path().stem().string() + ".mld.ab.compare.txt");
                writeBridgeAbComparison(compareOutPath, sa3dPortParsed, validBlocks, bridgeReportPaths);
            } else {
                spice::mld::parsing::ParseOptions parityOptions{};
                parityOptions.extractGrndGobjBlocks = operation.extractGrndGobjBlocks;
                auto parityParsed = mldParser.project(mldFile, parityOptions);

                const auto parityOutPath = outputDir / (entry.path().stem().string() + ".mld.parity.txt");
                std::ofstream parityOut(parityOutPath, std::ios::binary);
                parityOut << spice::mld::parsing::formatParseSummary(parityParsed);
                if (operation.extractGrndGobjBlocks) {
                    writeExtractedSpatialBlocks(context, outputDir, entry.path().stem().string(), parityParsed.extractedSpatialBlocks);
                }

                const auto jsonOutPath = outputDir / (entry.path().stem().string() + ".json");
                std::ofstream jsonOut(jsonOutPath, std::ios::binary);
                if (parityParsed.blenderIrScene.has_value()) {
                    jsonOut << exporter.toJson(*parityParsed.blenderIrScene).c_str();
                }
            }
            ++filesProcessed;
            continue;
        }
    }

    if (operation.kind == DirectoryOperationKind::ExportContentGraph) {
        spice::contentgraph::ContentGraphCorpusBuildOptions graphOptions{};
        graphOptions.sctOptions.detailLevel = spice::contentgraph::ContentGraphDetailLevel::Instructions;
        spice::contentgraph::ContentGraphCorpusBuilder graphBuilder{};
        const auto graph = graphBuilder.build(contentGraphInput, graphOptions);
        spice::contentgraph::ContentGraphJsonExporter graphExporter{};
        const auto graphOutPath = outputDir / "content_graph.json";
        std::ofstream graphOut(graphOutPath, std::ios::binary);
        spice::contentgraph::ContentGraphProjection projection = spice::contentgraph::ContentGraphProjection::Full;
        if (operation.contentGraphProjection == spice::mix::ContentGraphProjection::Sections) {
            projection = spice::contentgraph::ContentGraphProjection::Sections;
        } else if (operation.contentGraphProjection == spice::mix::ContentGraphProjection::World) {
            projection = spice::contentgraph::ContentGraphProjection::World;
        }
        graphOut << graphExporter.toJson(graph, projection);
        emit(context, spice::mix::EventLevel::Info,
            "  - Wrote content graph: ", graphOutPath.string());
    }

    if (operation.kind == DirectoryOperationKind::InventoryMldGvrFormats) {
        const auto inventory = mldGvrFormatInventoryBuilder.build();
        const auto jsonOutPath = outputDir / "mld_gvr_format_inventory.json";
        {
            std::ofstream jsonOut(jsonOutPath, std::ios::binary);
            jsonOut << spice::mld::analysis::formatMldGvrFormatInventoryJson(inventory);
        }
        const auto markdownOutPath = outputDir / "mld_gvr_format_priority_report.md";
        {
            std::ofstream markdownOut(markdownOutPath, std::ios::binary);
            markdownOut << spice::mld::analysis::formatMldGvrFormatInventoryMarkdown(inventory);
        }
        emit(context, spice::mix::EventLevel::Info,
            "  - Wrote MLD GVR format inventory: ", jsonOutPath.string());
        emit(context, spice::mix::EventLevel::Info,
            "  - Wrote MLD GVR priority report: ", markdownOutPath.string());
    }

    emit(context, spice::mix::EventLevel::Progress,
        "Step 4/4: Finalizing summary.");
    emit(context, spice::mix::EventLevel::Info, "Operation finished.");
    emit(context, spice::mix::EventLevel::Info, "FilesProcessed=", filesProcessed);
    emit(context, spice::mix::EventLevel::Info, "inputDir=", inputDir.string());
    emit(context, spice::mix::EventLevel::Info, "outputDir=", outputDir.string());

    return 0;
}

template <class... Visitors>
struct Overloaded : Visitors... {
    using Visitors::operator()...;
};

spice::gvm::ir::AklzPolicy toRuntimeAklzPolicy(const spice::mix::AklzPolicy policy) {
    switch (policy) {
    case spice::mix::AklzPolicy::Preserve:
        return spice::gvm::ir::AklzPolicy::Preserve;
    case spice::mix::AklzPolicy::Raw:
        return spice::gvm::ir::AklzPolicy::Raw;
    case spice::mix::AklzPolicy::Compressed:
        return spice::gvm::ir::AklzPolicy::Compressed;
    }
    return spice::gvm::ir::AklzPolicy::Preserve;
}

spice::gvm::model::TextureFormat toRuntimeTextureFormat(const spice::mix::GvrTextureFormat format) {
    using Source = spice::mix::GvrTextureFormat;
    using Target = spice::gvm::model::TextureFormat;
    switch (format) {
    case Source::I4: return Target::I4;
    case Source::I8: return Target::I8;
    case Source::IA4: return Target::IA4;
    case Source::IA8: return Target::IA8;
    case Source::RGB565: return Target::RGB565;
    case Source::RGB5A3: return Target::RGB5A3;
    case Source::RGBA8: return Target::RGBA8;
    case Source::CI4: return Target::CI4;
    case Source::CI8: return Target::CI8;
    case Source::CI14X2: return Target::CI14X2;
    case Source::CMPR: return Target::CMPR;
    }
    return Target::RGBA8;
}

spice::gvm::model::PaletteFormat toRuntimePaletteFormat(const spice::mix::GvrPaletteFormat format) {
    using Source = spice::mix::GvrPaletteFormat;
    using Target = spice::gvm::model::PaletteFormat;
    switch (format) {
    case Source::IA8: return Target::IA8;
    case Source::RGB565: return Target::RGB565;
    case Source::RGB5A3: return Target::RGB5A3;
    }
    return Target::RGB5A3;
}

DirectoryOperation makeDirectoryOperation(const spice::mix::ParseMldRequest& request) {
    DirectoryOperation operation{};
    operation.kind = DirectoryOperationKind::ParseMld;
    operation.paths = request.paths;
    operation.extractGrndGobjBlocks = request.extractGrndGobjBlocks;
    return operation;
}

DirectoryOperation makeDirectoryOperation(const spice::mix::CompareMldSa3dRequest& request) {
    DirectoryOperation operation{};
    operation.kind = DirectoryOperationKind::CompareMldSa3d;
    operation.paths = request.paths;
    operation.extractGrndGobjBlocks = request.extractGrndGobjBlocks;
    return operation;
}

DirectoryOperation makeDirectoryOperation(const spice::mix::ExportMldEntryListRequest& request) {
    DirectoryOperation operation{};
    operation.kind = DirectoryOperationKind::ExportMldEntryList;
    operation.paths = request.paths;
    return operation;
}

DirectoryOperation makeDirectoryOperation(const spice::mix::InventoryMldGvrFormatsRequest& request) {
    DirectoryOperation operation{};
    operation.kind = DirectoryOperationKind::InventoryMldGvrFormats;
    operation.paths = request.paths;
    return operation;
}

DirectoryOperation makeDirectoryOperation(const spice::mix::ParseSctRequest& request) {
    DirectoryOperation operation{};
    operation.kind = DirectoryOperationKind::ParseSct;
    operation.paths = request.paths;
    operation.decodeSctUnreachedCode = request.decodeUnreachedCode;
    return operation;
}

DirectoryOperation makeDirectoryOperation(const spice::mix::ExportSctRequest& request) {
    DirectoryOperation operation{};
    operation.kind = DirectoryOperationKind::ExportSct;
    operation.paths = request.paths;
    operation.decodeSctUnreachedCode = request.decodeUnreachedCode;
    operation.compressSctAklz = request.compressAklz;
    return operation;
}

DirectoryOperation makeDirectoryOperation(const spice::mix::ExportSmlResearchRequest& request) {
    DirectoryOperation operation{};
    operation.kind = DirectoryOperationKind::ExportSmlResearch;
    operation.paths.input = request.input;
    operation.paths.output = request.output;
    operation.annotationRepository = request.annotationRepository;
    operation.embeddedMld = request.embeddedMld;
    operation.embeddedMldBlenderIr = request.embeddedMldBlenderIr;
    operation.combinedBlenderIr = request.combinedBlenderIr;
    operation.combinedRawPlacement = request.combinedPlacement == spice::mix::CombinedPlacement::Raw;
    operation.commandMap = request.commandMap;
    return operation;
}

DirectoryOperation makeDirectoryOperation(const spice::mix::ExportStdJsonRequest& request) {
    DirectoryOperation operation{};
    operation.kind = DirectoryOperationKind::ExportStdJson;
    operation.paths.input = request.input;
    operation.paths.output = request.output;
    return operation;
}

DirectoryOperation makeDirectoryOperation(const spice::mix::ExportContentGraphRequest& request) {
    DirectoryOperation operation{};
    operation.kind = DirectoryOperationKind::ExportContentGraph;
    operation.paths = request.paths;
    operation.contentGraphProjection = request.projection;
    return operation;
}

DirectoryOperation makeDirectoryOperation(const spice::mix::CreateGvrBatchRequest& request) {
    DirectoryOperation operation{};
    operation.kind = DirectoryOperationKind::CreateGvrBatch;
    operation.paths.input = request.input;
    operation.paths.output = request.output;
    operation.encoding = request.encoding;
    return operation;
}

DirectoryOperation makeDirectoryOperation(const spice::mix::ReplaceGvrBatchRequest& request) {
    DirectoryOperation operation{};
    operation.kind = DirectoryOperationKind::ReplaceGvrBatch;
    operation.paths.input = request.input;
    operation.paths.output = request.output;
    operation.sourceGvrDirectory = request.sourceGvrDirectory;
    operation.encoding = request.encoding;
    return operation;
}

DirectoryOperation makeDirectoryOperation(const spice::mix::ExportGvrImageIrRequest& request) {
    DirectoryOperation operation{};
    operation.kind = DirectoryOperationKind::ExportGvrImageIr;
    operation.paths.input = request.input;
    operation.paths.output = request.output;
    return operation;
}

DirectoryOperation makeDirectoryOperation(const spice::mix::ImportGvrImageIrRequest& request) {
    DirectoryOperation operation{};
    operation.kind = DirectoryOperationKind::ImportGvrImageIr;
    operation.paths.input = request.input;
    operation.paths.output = request.output;
    operation.importAklz = request.aklz;
    return operation;
}

#include "MlkStdContentGraphOperations.inl"
void finishSingleFile(
    spice::mix::OperationContext& context,
    const std::filesystem::path& output) {
    emit(context, spice::mix::EventLevel::Progress,
        "Step 3/4: Wrote ", output.string());
    emit(context, spice::mix::EventLevel::Progress,
        "Step 4/4: Finalizing summary.");
    emit(context, spice::mix::EventLevel::Info, "Operation finished.");
    emit(context, spice::mix::EventLevel::Info, "FilesProcessed=1");
}

int executeOperationRequest(
    const spice::mix::OperationRequest& request,
    spice::mix::OperationContext& context) {
    emit(context, spice::mix::EventLevel::Progress,
        "Step 1/4: Initializing operation.");
    return std::visit(Overloaded{
        [&](const spice::mix::ParseMldRequest& value) {
            return executeDirectoryOperation(makeDirectoryOperation(value), context);
        },
        [&](const spice::mix::CompareMldSa3dRequest& value) {
            return executeDirectoryOperation(makeDirectoryOperation(value), context);
        },
        [&](const spice::mix::ExportMldEntryListRequest& value) {
            return executeDirectoryOperation(makeDirectoryOperation(value), context);
        },
        [&](const spice::mix::InventoryMldGvrFormatsRequest& value) {
            return executeDirectoryOperation(makeDirectoryOperation(value), context);
        },
        [&](const spice::mix::ReplaceMldTextureRequest& value) {
            emit(context, spice::mix::EventLevel::Progress,
                "Step 2/4: Replacing embedded MLD texture.");
            replaceMldTextureFromPngFile(value);
            finishSingleFile(context, value.output);
            return 0;
        },
        [&](const spice::mix::ExtractMldTextureGvrRequest& value) {
            emit(context, spice::mix::EventLevel::Progress,
                "Step 2/4: Extracting embedded MLD texture GVR.");
            extractMldTextureToGvrFile(value);
            finishSingleFile(context, value.output);
            return 0;
        },
        [&](const spice::mix::ExtractMldTexturePngRequest& value) {
            emit(context, spice::mix::EventLevel::Progress,
                "Step 2/4: Extracting embedded MLD texture PNG.");
            extractMldTextureToPngFile(value);
            finishSingleFile(context, value.output);
            return 0;
        },
        [&](const spice::mix::ParseSctRequest& value) {
            return executeDirectoryOperation(makeDirectoryOperation(value), context);
        },
        [&](const spice::mix::ExportSctRequest& value) {
            return executeDirectoryOperation(makeDirectoryOperation(value), context);
        },
        [&](const spice::mix::ExportSmlResearchRequest& value) {
            return executeDirectoryOperation(makeDirectoryOperation(value), context);
        },
        [&](const spice::mix::ExportStdJsonRequest& value) {
            return executeDirectoryOperation(makeDirectoryOperation(value), context);
        },
        [&](const spice::mix::ExportMlkCorpusRequest& value) {
            return executeMlkCorpus(value, context);
        },
        [&](const spice::mix::ExportMlkBlenderIrRequest& value) {
            return executeMlkBlenderIr(value, context);
        },
        [&](const spice::mix::ExportContentGraphRequest& value) {
            return executeDirectoryOperation(makeDirectoryOperation(value), context);
        },
        [&](const spice::mix::ExportAlxEnemyEventsRequest& value) {
            emit(context, spice::mix::EventLevel::Progress,
                "Step 2/4: Importing ALX enemy events.");
            spice::alx::exportEnemyEventsCsvToJson(value.input, value.output);
            finishSingleFile(context, value.output);
            return 0;
        },
        [&](const spice::mix::AuditDreamcastParityRequest& value) {
            return spice::mix::detail::executeDreamcastParityAudit(value, context);
        },
        [&](const spice::mix::CreateGvrRequest& value) {
            emit(context, spice::mix::EventLevel::Progress,
                "Step 2/4: Creating standalone GVR.");
            createGvrFromPngFile(value.encoding, value.input, value.output);
            finishSingleFile(context, value.output);
            return 0;
        },
        [&](const spice::mix::ReplaceGvrRequest& value) {
            emit(context, spice::mix::EventLevel::Progress,
                "Step 2/4: Replacing standalone GVR.");
            replaceGvrFromPngFile(value.encoding, value.source, value.replacement, value.output);
            finishSingleFile(context, value.output);
            return 0;
        },
        [&](const spice::mix::GvrToPngRequest& value) {
            emit(context, spice::mix::EventLevel::Progress,
                "Step 2/4: Decoding standalone GVR to PNG.");
            convertGvrToPngFile(value.input, value.output);
            finishSingleFile(context, value.output);
            return 0;
        },
        [&](const spice::mix::CreateGvrBatchRequest& value) {
            return executeDirectoryOperation(makeDirectoryOperation(value), context);
        },
        [&](const spice::mix::ReplaceGvrBatchRequest& value) {
            return executeDirectoryOperation(makeDirectoryOperation(value), context);
        },
        [&](const spice::mix::ExportGvrImageIrRequest& value) {
            return executeDirectoryOperation(makeDirectoryOperation(value), context);
        },
        [&](const spice::mix::ImportGvrImageIrRequest& value) {
            return executeDirectoryOperation(makeDirectoryOperation(value), context);
        },
        [&](const spice::mix::CompressAklzRequest& value) {
            emit(context, spice::mix::EventLevel::Progress,
                "Step 2/4: Compressing AKLZ.");
            runAklzUtility(value.input, value.output, false);
            finishSingleFile(context, value.output);
            return 0;
        },
        [&](const spice::mix::DecompressAklzRequest& value) {
            emit(context, spice::mix::EventLevel::Progress,
                "Step 2/4: Decompressing AKLZ.");
            runAklzUtility(value.input, value.output, true);
            finishSingleFile(context, value.output);
            return 0;
        },
    }, request);
}

} // namespace

namespace spice::mix::detail {

int executeOperation(const OperationRequest& request, OperationContext& context) {
    return executeOperationRequest(request, context);
}

} // namespace spice::mix::detail
