#include "MlkBlenderIrExport.h"

#include "MlkScanner.h"

#include "../Compression/Aklz.h"
#include "../SpiceMLD/Export/BlenderIrJsonExporter.h"
#include "../SpiceMLD/Parsing/MldParser.h"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <iterator>
#include <optional>
#include <set>
#include <span>
#include <sstream>
#include <stdexcept>
#include <string_view>
#include <unordered_map>

namespace spice::mlk {
namespace {

std::vector<std::uint8_t> readFileBytes(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        std::ostringstream message;
        message << "Could not open file: " << path.string();
        throw std::runtime_error(message.str());
    }

    in.seekg(0, std::ios::end);
    const auto size = in.tellg();
    if (size < 0) {
        std::ostringstream message;
        message << "Could not determine file size: " << path.string();
        throw std::runtime_error(message.str());
    }
    in.seekg(0, std::ios::beg);

    std::vector<std::uint8_t> bytes(static_cast<std::size_t>(size));
    if (!bytes.empty()) {
        in.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
        if (!in) {
            std::ostringstream message;
            message << "Could not read full file: " << path.string();
            throw std::runtime_error(message.str());
        }
    }
    return bytes;
}

void writeTextFile(const std::filesystem::path& path, const std::string& text) {
    std::filesystem::create_directories(path.parent_path());
    std::ofstream out(path, std::ios::binary);
    if (!out) {
        std::ostringstream message;
        message << "Could not open output file: " << path.string();
        throw std::runtime_error(message.str());
    }
    out << text;
    if (!out.good()) {
        std::ostringstream message;
        message << "Could not write output file: " << path.string();
        throw std::runtime_error(message.str());
    }
}

std::string toLowerCopy(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return value;
}

bool isMlkPath(const std::filesystem::path& path) {
    return toLowerCopy(path.extension().string()) == ".mlk";
}

std::vector<std::filesystem::path> collectMlkPaths(const std::filesystem::path& inputPath, bool& inputWasDirectory) {
    std::error_code ec{};
    inputWasDirectory = std::filesystem::is_directory(inputPath, ec);
    ec.clear();
    const bool inputWasFile = std::filesystem::is_regular_file(inputPath, ec);
    if (!inputWasDirectory && !inputWasFile) {
        std::ostringstream message;
        message << "Input path is not a file or directory: " << inputPath.string();
        throw std::runtime_error(message.str());
    }

    std::vector<std::filesystem::path> paths{};
    if (inputWasFile) {
        if (isMlkPath(inputPath)) {
            paths.push_back(inputPath);
        }
        return paths;
    }

    for (const auto& entry : std::filesystem::recursive_directory_iterator(
             inputPath,
             std::filesystem::directory_options::skip_permission_denied)) {
        std::error_code entryEc{};
        if (!entry.is_regular_file(entryEc) || entryEc) {
            continue;
        }
        if (isMlkPath(entry.path())) {
            paths.push_back(entry.path());
        }
    }
    std::sort(paths.begin(), paths.end());
    return paths;
}

std::string relativePathString(
    const std::filesystem::path& path,
    const std::filesystem::path& inputPath,
    bool inputWasDirectory) {
    if (!inputWasDirectory) {
        return path.filename().generic_string();
    }

    std::error_code ec{};
    const auto relative = std::filesystem::relative(path, inputPath, ec);
    if (!ec && !relative.empty()) {
        return relative.generic_string();
    }
    return path.filename().generic_string();
}

std::vector<std::uint8_t> decodeForPayloadAccess(std::span<const std::uint8_t> bytes) {
    if (!spice::compression::aklz::isAklz(bytes)) {
        return std::vector<std::uint8_t>(bytes.begin(), bytes.end());
    }
    const auto decoded = spice::compression::aklz::decompress(bytes);
    if (!decoded.ok()) {
        return {};
    }
    return decoded.bytes;
}

std::string makeSafeNameComponent(std::string value, std::string fallback) {
    for (char& c : value) {
        const auto uc = static_cast<unsigned char>(c);
        if (!std::isalnum(uc) && c != '_' && c != '-') {
            c = '_';
        }
    }

    const auto first = std::find_if(value.begin(), value.end(), [](char c) {
        return c != '_';
    });
    const auto last = std::find_if(value.rbegin(), value.rend(), [](char c) {
        return c != '_';
    }).base();
    if (first >= last) {
        return fallback;
    }
    return std::string(first, last);
}

std::string makeRecordPrefix(const std::string& stem, std::size_t recordIndex) {
    const auto safeStem = makeSafeNameComponent(stem, "mlk");
    return safeStem + "_" + std::to_string(recordIndex) + "_";
}

void namespaceRecordScene(
    spice::mld::model::BlenderIrScene& scene,
    const std::string& stem,
    std::size_t recordIndex) {
    const auto prefix = makeRecordPrefix(stem, recordIndex);
    std::unordered_map<std::string, std::string> renamedByOriginalName{};

    for (auto& texture : scene.textures) {
        const auto originalName = texture.textureName;
        const auto fallback = texture.hasTextureId
            ? "texture_" + std::to_string(texture.textureId)
            : "texture";
        const auto safeTextureName = makeSafeNameComponent(originalName, fallback);
        const auto namespacedName = prefix + safeTextureName;
        texture.textureName = namespacedName;
        if (!originalName.empty()) {
            renamedByOriginalName.emplace(originalName, namespacedName);
        }
        scene.diagnostics.push_back("MLK export namespaced texture " + originalName + " -> " + namespacedName);
    }

    for (auto& mesh : scene.meshes) {
        for (auto& material : mesh.materials) {
            const auto it = renamedByOriginalName.find(material.textureName);
            if (it != renamedByOriginalName.end()) {
                material.textureName = it->second;
            } else if (!material.textureName.empty()) {
                material.textureName = prefix + makeSafeNameComponent(material.textureName, "texture");
            }
        }
    }

    for (auto& tree : scene.objectTrees) {
        tree.label = prefix + makeSafeNameComponent(tree.label, "object_tree");
    }
}

struct RecordSceneAppendResult {
    std::size_t meshIndexStart{ 0U };
    std::size_t objectTreeIndexStart{ 0U };
    std::size_t entryIndexStart{ 0U };
    std::size_t textureIndexStart{ 0U };
    std::size_t animationIndexStart{ 0U };
    std::vector<MlkBlenderIrEntryMetadata> entries{};
};

class MlkBlenderIrCombiner {
public:
    [[nodiscard]] RecordSceneAppendResult appendRecordScene(
        spice::mld::model::BlenderIrScene recordScene,
        const std::string& stem,
        std::size_t recordIndex) {
        namespaceRecordScene(recordScene, stem, recordIndex);

        const auto meshIndexBase = scene_.meshes.size();
        const auto objectTreeIndexBase = scene_.objectTrees.size();
        const auto indexEntryBase = scene_.indexEntries.size();
        const auto textureIndexBase = scene_.textures.size();
        const auto animationIndexBase = scene_.animations.size();

        RecordSceneAppendResult appendResult{
            .meshIndexStart = meshIndexBase,
            .objectTreeIndexStart = objectTreeIndexBase,
            .entryIndexStart = indexEntryBase,
            .textureIndexStart = textureIndexBase,
            .animationIndexStart = animationIndexBase,
        };

        for (auto& tree : recordScene.objectTrees) {
            for (auto& node : tree.nodes) {
                if (node.meshIndex.has_value()) {
                    *node.meshIndex += meshIndexBase;
                }
            }
        }

        for (std::size_t localEntryIndex = 0; localEntryIndex < recordScene.indexEntries.size(); ++localEntryIndex) {
            auto& indexEntry = recordScene.indexEntries[localEntryIndex];
            const auto combinedEntryIndex = indexEntryBase + localEntryIndex;
            const auto originalSourceEntryId = indexEntry.sourceEntryId;
            const auto originalTableIndex = indexEntry.tableIndex;
            const auto originalFxnName = indexEntry.fxnName;
            const auto adjustedFxnName = makeRecordPrefix(stem, recordIndex) +
                makeSafeNameComponent(originalFxnName, "entry");
            indexEntry.sourceEntryId = static_cast<std::uint32_t>(combinedEntryIndex);
            indexEntry.fxnName = adjustedFxnName;
            indexEntry.tableIndex += indexEntryBase;
            for (auto& meshIndex : indexEntry.meshIndices) {
                meshIndex += meshIndexBase;
            }
            for (auto& objectTreeIndex : indexEntry.objectTreeIndices) {
                objectTreeIndex += objectTreeIndexBase;
            }
            appendResult.entries.push_back(MlkBlenderIrEntryMetadata{
                .combinedEntryIndex = combinedEntryIndex,
                .combinedSourceEntryId = indexEntry.sourceEntryId,
                .originalSourceEntryId = originalSourceEntryId,
                .originalTableIndex = originalTableIndex,
                .originalFxnName = originalFxnName,
                .adjustedFxnName = adjustedFxnName,
            });
            scene_.indexEntries.push_back(std::move(indexEntry));
        }

        for (auto& animation : recordScene.animations) {
            animation.tableIndex += indexEntryBase;
            animation.objectTreeIndex += objectTreeIndexBase;
            if (animation.tableIndex >= indexEntryBase &&
                animation.tableIndex < indexEntryBase + recordScene.indexEntries.size()) {
                animation.sourceEntryId = static_cast<std::uint32_t>(animation.tableIndex);
            }
            scene_.animations.push_back(std::move(animation));
        }

        scene_.meshes.insert(
            scene_.meshes.end(),
            std::make_move_iterator(recordScene.meshes.begin()),
            std::make_move_iterator(recordScene.meshes.end()));
        scene_.objectTrees.insert(
            scene_.objectTrees.end(),
            std::make_move_iterator(recordScene.objectTrees.begin()),
            std::make_move_iterator(recordScene.objectTrees.end()));
        scene_.textures.insert(
            scene_.textures.end(),
            std::make_move_iterator(recordScene.textures.begin()),
            std::make_move_iterator(recordScene.textures.end()));
        scene_.diagnostics.push_back("MLK combined Blender IR appended record " + std::to_string(recordIndex) +
            " from prefix " + makeRecordPrefix(stem, recordIndex));
        scene_.diagnostics.insert(
            scene_.diagnostics.end(),
            std::make_move_iterator(recordScene.diagnostics.begin()),
            std::make_move_iterator(recordScene.diagnostics.end()));
        return appendResult;
    }

    [[nodiscard]] const spice::mld::model::BlenderIrScene& scene() const noexcept {
        return scene_;
    }

private:
    spice::mld::model::BlenderIrScene scene_{};
};

void appendJsonEscaped(std::ostream& out, std::string_view value) {
    for (const char ch : value) {
        switch (ch) {
        case '\\':
            out << "\\\\";
            break;
        case '"':
            out << "\\\"";
            break;
        case '\b':
            out << "\\b";
            break;
        case '\f':
            out << "\\f";
            break;
        case '\n':
            out << "\\n";
            break;
        case '\r':
            out << "\\r";
            break;
        case '\t':
            out << "\\t";
            break;
        default:
            if (static_cast<unsigned char>(ch) < 0x20U) {
                out << "\\u00";
                constexpr char kHex[] = "0123456789abcdef";
                out << kHex[(static_cast<unsigned char>(ch) >> 4U) & 0x0fU];
                out << kHex[static_cast<unsigned char>(ch) & 0x0fU];
            } else {
                out << ch;
            }
            break;
        }
    }
}

void writeJsonString(std::ostream& out, std::string_view value) {
    out << '"';
    appendJsonEscaped(out, value);
    out << '"';
}

std::string csvEscape(std::string_view value) {
    const bool needsQuotes = value.find_first_of(",\"\r\n") != std::string_view::npos;
    if (!needsQuotes) {
        return std::string(value);
    }

    std::string escaped;
    escaped.reserve(value.size() + 2U);
    escaped.push_back('"');
    for (const char ch : value) {
        if (ch == '"') {
            escaped.push_back('"');
        }
        escaped.push_back(ch);
    }
    escaped.push_back('"');
    return escaped;
}

std::string boolText(bool value) {
    return value ? "true" : "false";
}

std::string joinSampleNames(const std::vector<std::string>& values) {
    std::ostringstream out;
    for (std::size_t i = 0; i < values.size(); ++i) {
        if (i != 0U) {
            out << "|";
        }
        out << values[i];
    }
    return out.str();
}

void collectSampleFxnNames(
    const spice::mld::parsing::ParseResult& parsed,
    MlkBlenderIrRecordExportSummary& summary) {
    std::set<std::string> seen{};
    for (const auto& entry : parsed.entryList) {
        if (!entry.fxnName.empty() && seen.insert(entry.fxnName).second &&
            summary.sampleFxnNames.size() < 8U) {
            summary.sampleFxnNames.push_back(entry.fxnName);
        }
    }
}

std::filesystem::path outputDirForFile(
    const std::filesystem::path& outputRoot,
    const std::string& relativePath) {
    const auto relative = std::filesystem::path(relativePath);
    const auto parent = relative.parent_path();
    const auto stem = relative.stem();
    return outputRoot / parent / stem;
}

std::string formatRecordsCsv(const MlkBlenderIrFileExportResult& file) {
    std::ostringstream out;
    out << "filePath,recordIndex,recordOffset,key,generatedMldName,rawWord12,payloadOffset,payloadSize,"
           "payloadKind,payloadInBounds,parseAttempted,parseOk,status,skipReason,diagnosticCount,"
           "warningCount,errorCount,meshCount,objectTreeCount,indexEntryCount,textureCount,animationCount,"
           "sampleFxnNames\n";
    for (const auto& record : file.records) {
        out << csvEscape(record.filePath) << ','
            << record.recordIndex << ','
            << record.recordOffset << ','
            << record.key << ','
            << csvEscape(record.generatedMldName) << ','
            << record.rawWord12 << ','
            << record.payloadOffset << ','
            << record.payloadSize << ','
            << csvEscape(record.payloadKind) << ','
            << boolText(record.payloadInBounds) << ','
            << boolText(record.parseAttempted) << ','
            << boolText(record.parseOk) << ','
            << csvEscape(record.status) << ','
            << csvEscape(record.skipReason) << ','
            << record.diagnosticCount << ','
            << record.warningCount << ','
            << record.errorCount << ','
            << record.meshCount << ','
            << record.objectTreeCount << ','
            << record.indexEntryCount << ','
            << record.textureCount << ','
            << record.animationCount << ','
            << csvEscape(joinSampleNames(record.sampleFxnNames)) << '\n';
    }
    return out.str();
}

std::string formatManifestJson(const MlkBlenderIrFileExportResult& file) {
    std::ostringstream out;
    out << "{\n";
    out << "  \"path\": ";
    writeJsonString(out, file.relativePath);
    out << ",\n";
    out << "  \"recordCount\": " << file.recordCount << ",\n";
    out << "  \"parsedRecordCount\": " << file.parsedRecordCount << ",\n";
    out << "  \"skippedRecordCount\": " << file.skippedRecordCount << ",\n";
    out << "  \"combinedBlenderIrScene\": ";
    writeJsonString(out, file.combinedBlenderIrPath.filename().generic_string());
    out << ",\n";
    out << "  \"metadata\": ";
    writeJsonString(out, file.metadataPath.filename().generic_string());
    out << ",\n";
    out << "  \"recordsCsv\": ";
    writeJsonString(out, file.recordsCsvPath.filename().generic_string());
    out << ",\n";
    out << "  \"records\": [\n";
    for (std::size_t i = 0; i < file.records.size(); ++i) {
        if (i != 0U) {
            out << ",\n";
        }
        const auto& record = file.records[i];
        out << "    {\n";
        out << "      \"recordIndex\": " << record.recordIndex << ",\n";
        out << "      \"recordOffset\": " << record.recordOffset << ",\n";
        out << "      \"key\": " << record.key << ",\n";
        out << "      \"generatedMldName\": ";
        writeJsonString(out, record.generatedMldName);
        out << ",\n";
        out << "      \"rawWord12\": " << record.rawWord12 << ",\n";
        out << "      \"payloadOffset\": " << record.payloadOffset << ",\n";
        out << "      \"payloadSize\": " << record.payloadSize << ",\n";
        out << "      \"payloadKind\": ";
        writeJsonString(out, record.payloadKind);
        out << ",\n";
        out << "      \"payloadInBounds\": " << boolText(record.payloadInBounds) << ",\n";
        out << "      \"parseAttempted\": " << boolText(record.parseAttempted) << ",\n";
        out << "      \"parseOk\": " << boolText(record.parseOk) << ",\n";
        out << "      \"status\": ";
        writeJsonString(out, record.status);
        out << ",\n";
        out << "      \"skipReason\": ";
        writeJsonString(out, record.skipReason);
        out << ",\n";
        out << "      \"diagnosticCount\": " << record.diagnosticCount << ",\n";
        out << "      \"warningCount\": " << record.warningCount << ",\n";
        out << "      \"errorCount\": " << record.errorCount << ",\n";
        out << "      \"meshCount\": " << record.meshCount << ",\n";
        out << "      \"objectTreeCount\": " << record.objectTreeCount << ",\n";
        out << "      \"indexEntryCount\": " << record.indexEntryCount << ",\n";
        out << "      \"textureCount\": " << record.textureCount << ",\n";
        out << "      \"animationCount\": " << record.animationCount << ",\n";
        out << "      \"sampleFxnNames\": [";
        for (std::size_t j = 0; j < record.sampleFxnNames.size(); ++j) {
            if (j != 0U) {
                out << ", ";
            }
            writeJsonString(out, record.sampleFxnNames[j]);
        }
        out << "]\n";
        out << "    }";
    }
    out << "\n";
    out << "  ]\n";
    out << "}\n";
    return out.str();
}

std::string formatMetadataJson(const MlkBlenderIrFileExportResult& file) {
    std::ostringstream out;
    out << "{\n";
    out << "  \"$schema\": \"MlkBlenderIrMetadata.schema.json\",\n";
    out << "  \"schemaVersion\": 1,\n";
    out << "  \"containerKind\": \"mlk\",\n";
    out << "  \"containerPath\": ";
    writeJsonString(out, file.relativePath);
    out << ",\n";
    out << "  \"combinedBlenderIrScene\": ";
    writeJsonString(out, file.combinedBlenderIrPath.filename().generic_string());
    out << ",\n";
    out << "  \"manifest\": ";
    writeJsonString(out, file.manifestPath.filename().generic_string());
    out << ",\n";
    out << "  \"recordsCsv\": ";
    writeJsonString(out, file.recordsCsvPath.filename().generic_string());
    out << ",\n";
    out << "  \"records\": [\n";
    for (std::size_t i = 0; i < file.records.size(); ++i) {
        if (i != 0U) {
            out << ",\n";
        }
        const auto& record = file.records[i];
        out << "    {\n";
        out << "      \"recordIndex\": " << record.recordIndex << ",\n";
        out << "      \"recordOffset\": " << record.recordOffset << ",\n";
        out << "      \"key\": " << record.key << ",\n";
        out << "      \"generatedMldName\": ";
        writeJsonString(out, record.generatedMldName);
        out << ",\n";
        out << "      \"rawWord12\": " << record.rawWord12 << ",\n";
        out << "      \"payloadOffset\": " << record.payloadOffset << ",\n";
        out << "      \"payloadSize\": " << record.payloadSize << ",\n";
        out << "      \"payloadKind\": ";
        writeJsonString(out, record.payloadKind);
        out << ",\n";
        out << "      \"status\": ";
        writeJsonString(out, record.status);
        out << ",\n";
        out << "      \"skipReason\": ";
        writeJsonString(out, record.skipReason);
        out << ",\n";
        out << "      \"combinedRanges\": {\n";
        out << "        \"meshIndexStart\": " << record.combinedMeshIndexStart << ",\n";
        out << "        \"meshCount\": " << record.meshCount << ",\n";
        out << "        \"objectTreeIndexStart\": " << record.combinedObjectTreeIndexStart << ",\n";
        out << "        \"objectTreeCount\": " << record.objectTreeCount << ",\n";
        out << "        \"entryIndexStart\": " << record.combinedEntryIndexStart << ",\n";
        out << "        \"entryCount\": " << record.indexEntryCount << ",\n";
        out << "        \"textureIndexStart\": " << record.combinedTextureIndexStart << ",\n";
        out << "        \"textureCount\": " << record.textureCount << ",\n";
        out << "        \"animationIndexStart\": " << record.combinedAnimationIndexStart << ",\n";
        out << "        \"animationCount\": " << record.animationCount << "\n";
        out << "      },\n";
        out << "      \"entries\": [\n";
        for (std::size_t j = 0; j < record.entries.size(); ++j) {
            if (j != 0U) {
                out << ",\n";
            }
            const auto& entry = record.entries[j];
            out << "        {\n";
            out << "          \"combinedEntryIndex\": " << entry.combinedEntryIndex << ",\n";
            out << "          \"combinedSourceEntryId\": " << entry.combinedSourceEntryId << ",\n";
            out << "          \"originalSourceEntryId\": " << entry.originalSourceEntryId << ",\n";
            out << "          \"originalTableIndex\": " << entry.originalTableIndex << ",\n";
            out << "          \"originalFxnName\": ";
            writeJsonString(out, entry.originalFxnName);
            out << ",\n";
            out << "          \"adjustedFxnName\": ";
            writeJsonString(out, entry.adjustedFxnName);
            out << "\n";
            out << "        }";
        }
        out << "\n";
        out << "      ]\n";
        out << "    }";
    }
    out << "\n";
    out << "  ]\n";
    out << "}\n";
    return out.str();
}

std::filesystem::path annotationDirForFile(
    const std::filesystem::path& annotationRepositoryDir,
    const std::string& relativePath) {
    return outputDirForFile(annotationRepositoryDir, relativePath);
}

std::size_t diagnosticCount(const MlkScanResult& scan, DiagnosticSeverity severity) {
    return static_cast<std::size_t>(std::count_if(scan.diagnostics.begin(), scan.diagnostics.end(), [&](const auto& diagnostic) {
        return diagnostic.severity == severity;
    }));
}

void writeHumanAnnotationTemplate(std::ostream& out, const std::string& indent) {
    out << "{\n"
        << indent << "  \"visualRole\": \"\",\n"
        << indent << "  \"description\": \"\",\n"
        << indent << "  \"visibleInBlender\": null,\n"
        << indent << "  \"visibleInGame\": null,\n"
        << indent << "  \"actorOrEffectRole\": \"\",\n"
        << indent << "  \"animationNotes\": \"\",\n"
        << indent << "  \"cameraOrHelperNotes\": \"\",\n"
        << indent << "  \"rawWord12Notes\": \"\",\n"
        << indent << "  \"relatedRecords\": [],\n"
        << indent << "  \"suspectedRuntimeBehavior\": \"\",\n"
        << indent << "  \"confidence\": \"\",\n"
        << indent << "  \"reviewedBy\": \"\",\n"
        << indent << "  \"reviewedAt\": \"\",\n"
        << indent << "  \"media\": []\n"
        << indent << "}";
}

void writeFileNotesTemplate(std::ostream& out, const std::string& indent) {
    out << "{\n"
        << indent << "  \"likelyUse\": \"\",\n"
        << indent << "  \"filenamePatternNotes\": \"\",\n"
        << indent << "  \"battleContextNotes\": \"\",\n"
        << indent << "  \"runtimeCorrelationNotes\": \"\",\n"
        << indent << "  \"rawWord12Hypothesis\": \"\",\n"
        << indent << "  \"openQuestions\": \"\",\n"
        << indent << "  \"reviewedBy\": \"\",\n"
        << indent << "  \"reviewedAt\": \"\",\n"
        << indent << "  \"resources\": []\n"
        << indent << "}";
}

void writeCombinedRanges(std::ostream& out, const MlkBlenderIrRecordExportSummary& record, const std::string& indent) {
    out << "{\n";
    out << indent << "  \"meshIndexStart\": " << record.combinedMeshIndexStart << ",\n";
    out << indent << "  \"meshCount\": " << record.meshCount << ",\n";
    out << indent << "  \"objectTreeIndexStart\": " << record.combinedObjectTreeIndexStart << ",\n";
    out << indent << "  \"objectTreeCount\": " << record.objectTreeCount << ",\n";
    out << indent << "  \"entryIndexStart\": " << record.combinedEntryIndexStart << ",\n";
    out << indent << "  \"entryCount\": " << record.indexEntryCount << ",\n";
    out << indent << "  \"textureIndexStart\": " << record.combinedTextureIndexStart << ",\n";
    out << indent << "  \"textureCount\": " << record.textureCount << ",\n";
    out << indent << "  \"animationIndexStart\": " << record.combinedAnimationIndexStart << ",\n";
    out << indent << "  \"animationCount\": " << record.animationCount << "\n";
    out << indent << "}";
}

void writeEntryMetadataArray(std::ostream& out,
    const std::vector<MlkBlenderIrEntryMetadata>& entries,
    const std::string& indent) {
    out << "[";
    for (std::size_t i = 0; i < entries.size(); ++i) {
        if (i != 0U) {
            out << ",";
        }
        const auto& entry = entries[i];
        out << "\n" << indent << "{\n";
        out << indent << "  \"combinedEntryIndex\": " << entry.combinedEntryIndex << ",\n";
        out << indent << "  \"combinedSourceEntryId\": " << entry.combinedSourceEntryId << ",\n";
        out << indent << "  \"originalSourceEntryId\": " << entry.originalSourceEntryId << ",\n";
        out << indent << "  \"originalTableIndex\": " << entry.originalTableIndex << ",\n";
        out << indent << "  \"originalFxnName\": ";
        writeJsonString(out, entry.originalFxnName);
        out << ",\n";
        out << indent << "  \"adjustedFxnName\": ";
        writeJsonString(out, entry.adjustedFxnName);
        out << "\n";
        out << indent << "}";
    }
    if (!entries.empty()) {
        out << "\n" << indent.substr(2U);
    }
    out << "]";
}

std::string formatAnnotationJson(
    const MlkBlenderIrFileExportResult& file,
    const MlkScanResult& scan,
    const std::filesystem::path& sourcePath) {
    const auto stem = std::filesystem::path(file.relativePath).stem().generic_string();
    std::ostringstream out;
    out << "{\n";
    out << "  \"$schema\": \"MlkAnnotation.schema.json\",\n";
    out << "  \"schema\": \"spice_mlk_annotation_v1\",\n";
    out << "  \"documentRole\": \"living_mlk_annotation\",\n";
    out << "  \"schemaVersion\": 1,\n";
    out << "  \"fileStem\": ";
    writeJsonString(out, stem);
    out << ",\n";
    out << "  \"sourceMlk\": ";
    writeJsonString(out, sourcePath.generic_string());
    out << ",\n";
    out << "  \"relativePath\": ";
    writeJsonString(out, file.relativePath);
    out << ",\n";
    out << "  \"mediaDirectory\": ";
    writeJsonString(out, file.annotationMediaDir.filename().generic_string());
    out << ",\n";
    out << "  \"combinedBlenderIrScene\": ";
    if (!file.annotationCombinedBlenderIrPath.empty()) {
        writeJsonString(out, file.annotationCombinedBlenderIrPath.filename().generic_string());
    } else {
        writeJsonString(out, file.combinedBlenderIrPath.generic_string());
    }
    out << ",\n";
    out << "  \"metadata\": ";
    writeJsonString(out, file.metadataPath.generic_string());
    out << ",\n";
    out << "  \"manifest\": ";
    writeJsonString(out, file.manifestPath.generic_string());
    out << ",\n";
    out << "  \"recordsCsv\": ";
    writeJsonString(out, file.recordsCsvPath.generic_string());
    out << ",\n";
    out << "  \"instructions\": \"Fill fileNotes and per-record humanAnnotations from Blender/in-game observation; keep computed fields as the current parser-derived snapshot. Re-exports preserve this file unless overwrite is explicitly requested.\",\n";
    out << "  \"fileNotes\": ";
    writeFileNotesTemplate(out, "  ");
    out << ",\n";
    out << "  \"computed\": {\n";
    out << "    \"rawSize\": " << scan.rawSize << ",\n";
    out << "    \"decodedSize\": " << scan.decodedSize << ",\n";
    out << "    \"sourceWasCompressedAklz\": " << boolText(scan.sourceWasCompressedAklz) << ",\n";
    out << "    \"headerWords\": [";
    for (std::size_t i = 0; i < scan.headerWords.size(); ++i) {
        out << (i == 0U ? "" : ", ") << scan.headerWords[i];
    }
    out << "],\n";
    out << "    \"headerRecordCountCandidate\": " << scan.recordCountCandidate << ",\n";
    out << "    \"selectedRecordCount\": " << scan.selectedRecordCount << ",\n";
    out << "    \"recordCountSource\": ";
    writeJsonString(out, toString(scan.recordCountSource));
    out << ",\n";
    out << "    \"firstPayloadOffset\": " << scan.firstPayloadOffset << ",\n";
    out << "    \"recordCountInferredFromFirstPayloadOffset\": " << scan.recordCountInferredFromFirstPayloadOffset << ",\n";
    out << "    \"recordTableBoundsStatus\": ";
    writeJsonString(out, scan.recordTableInBounds ? "in-bounds" : "out-of-bounds");
    out << ",\n";
    out << "    \"diagnosticCounts\": {\n";
    out << "      \"info\": " << diagnosticCount(scan, DiagnosticSeverity::Info) << ",\n";
    out << "      \"warning\": " << diagnosticCount(scan, DiagnosticSeverity::Warning) << ",\n";
    out << "      \"error\": " << diagnosticCount(scan, DiagnosticSeverity::Error) << "\n";
    out << "    },\n";
    out << "    \"exports\": {\n";
    out << "      \"combinedBlenderIrScene\": ";
    writeJsonString(out, file.combinedBlenderIrPath.generic_string());
    out << ",\n";
    out << "      \"metadata\": ";
    writeJsonString(out, file.metadataPath.generic_string());
    out << ",\n";
    out << "      \"manifest\": ";
    writeJsonString(out, file.manifestPath.generic_string());
    out << ",\n";
    out << "      \"recordsCsv\": ";
    writeJsonString(out, file.recordsCsvPath.generic_string());
    out << "\n";
    out << "    }\n";
    out << "  },\n";
    out << "  \"records\": [";
    for (std::size_t i = 0; i < file.records.size(); ++i) {
        if (i != 0U) {
            out << ",";
        }
        const auto& record = file.records[i];
        out << "\n";
        out << "    {\n";
        out << "      \"recordIndex\": " << record.recordIndex << ",\n";
        out << "      \"recordOffset\": " << record.recordOffset << ",\n";
        out << "      \"key\": " << record.key << ",\n";
        out << "      \"rawWord12\": " << record.rawWord12 << ",\n";
        out << "      \"payloadOffset\": " << record.payloadOffset << ",\n";
        out << "      \"payloadSize\": " << record.payloadSize << ",\n";
        out << "      \"payloadKind\": ";
        writeJsonString(out, record.payloadKind);
        out << ",\n";
        out << "      \"payloadInBounds\": " << boolText(record.payloadInBounds) << ",\n";
        out << "      \"status\": ";
        writeJsonString(out, record.status);
        out << ",\n";
        out << "      \"skipReason\": ";
        writeJsonString(out, record.skipReason);
        out << ",\n";
        out << "      \"generatedMldName\": ";
        writeJsonString(out, record.generatedMldName);
        out << ",\n";
        out << "      \"combinedRanges\": ";
        writeCombinedRanges(out, record, "      ");
        out << ",\n";
        out << "      \"entries\": ";
        writeEntryMetadataArray(out, record.entries, "        ");
        out << ",\n";
        out << "      \"humanAnnotations\": ";
        writeHumanAnnotationTemplate(out, "      ");
        out << "\n";
        out << "    }";
    }
    out << "\n";
    out << "  ]\n";
    out << "}\n";
    return out.str();
}

MlkBlenderIrRecordExportSummary makeInitialRecordSummary(
    const std::string& relativePath,
    const MlkRecordProbe& record) {
    MlkBlenderIrRecordExportSummary summary{};
    summary.filePath = relativePath;
    summary.recordIndex = record.index;
    summary.recordOffset = record.recordOffset;
    summary.key = record.key;
    summary.generatedMldName = generatedMldNameForKey(record.key);
    summary.rawWord12 = record.rawWord12;
    summary.payloadOffset = record.payloadOffset;
    summary.payloadSize = record.payloadSize;
    summary.payloadKind = toString(record.payloadKind);
    summary.payloadInBounds = record.payloadInBounds;
    return summary;
}

MlkBlenderIrFileExportResult exportFile(
    const std::filesystem::path& path,
    const std::filesystem::path& inputPath,
    bool inputWasDirectory,
    const std::filesystem::path& outputRoot,
    const MlkBlenderIrExportOptions& options) {
    const auto rawBytes = readFileBytes(path);
    const auto relativePath = relativePathString(path, inputPath, inputWasDirectory);
    const auto decodedBytes = decodeForPayloadAccess(rawBytes);
    const auto decodedSpan = std::span<const std::uint8_t>(decodedBytes.data(), decodedBytes.size());
    const auto scan = MlkScanner::scan(rawBytes, relativePath);
    const auto stem = path.stem().string();

    MlkBlenderIrFileExportResult result{};
    result.relativePath = relativePath;
    result.outputDir = outputDirForFile(outputRoot, relativePath);
    result.combinedBlenderIrPath = result.outputDir / (stem + "_mlk_combined_blender_ir_scene.json");
    result.manifestPath = result.outputDir / (stem + "_mlk_blender_manifest.json");
    result.metadataPath = result.outputDir / (stem + "_mlk_blender_metadata.json");
    result.recordsCsvPath = result.outputDir / (stem + "_mlk_blender_records.csv");
    if (!options.annotationRepositoryDir.empty()) {
        const auto annotationDir = annotationDirForFile(options.annotationRepositoryDir, relativePath);
        result.annotationPath = annotationDir / (stem + ".mlk_annotation.json");
        result.annotationMediaDir = annotationDir / (stem + ".mlk_annotation_media");
        result.annotationCombinedBlenderIrPath = annotationDir / result.combinedBlenderIrPath.filename();
    }
    result.recordCount = scan.records.size();
    result.records.reserve(scan.records.size());

    MlkBlenderIrCombiner combiner{};
    spice::mld::parsing::MldParser parser{};
    spice::mld::exporting::BlenderIrJsonExporter exporter{};

    for (const auto& record : scan.records) {
        auto summary = makeInitialRecordSummary(relativePath, record);
        if (!record.payloadInBounds) {
            summary.status = "skipped";
            summary.skipReason = "payload-out-of-bounds";
            ++result.skippedRecordCount;
            result.records.push_back(std::move(summary));
            continue;
        }
        if (record.payloadKind != MlkPayloadKind::MldFile) {
            summary.status = "skipped";
            summary.skipReason = "payload-kind-not-mld";
            ++result.skippedRecordCount;
            result.records.push_back(std::move(summary));
            continue;
        }
        if (record.payloadOffset > decodedSpan.size() ||
            record.payloadSize > decodedSpan.size() - record.payloadOffset) {
            summary.status = "skipped";
            summary.skipReason = "decoded-payload-out-of-bounds";
            ++result.skippedRecordCount;
            result.records.push_back(std::move(summary));
            continue;
        }

        summary.parseAttempted = true;
        const auto payload = decodedSpan.subspan(record.payloadOffset, record.payloadSize);

        spice::mld::parsing::ParseOptions parseOptions{};
        parseOptions.buildBlenderIntermediateIr = true;
        parseOptions.exportBlenderIrJson = false;
        parseOptions.extractGrndGobjBlocks = false;

        try {
            auto parsed = parser.parse(payload, parseOptions);
            summary.diagnosticCount = parsed.diagnostics.size();
            for (const auto& diagnostic : parsed.diagnostics) {
                if (diagnostic.severity == spice::mld::parsing::ParseDiagnostic::Severity::Warning) {
                    ++summary.warningCount;
                } else if (diagnostic.severity == spice::mld::parsing::ParseDiagnostic::Severity::Error) {
                    ++summary.errorCount;
                }
            }
            collectSampleFxnNames(parsed, summary);
            if (!parsed.blenderIrScene.has_value()) {
                summary.status = "skipped";
                summary.skipReason = "blender-ir-not-produced";
                ++result.skippedRecordCount;
                result.records.push_back(std::move(summary));
                continue;
            }

            summary.meshCount = parsed.blenderIrScene->meshes.size();
            summary.objectTreeCount = parsed.blenderIrScene->objectTrees.size();
            summary.indexEntryCount = parsed.blenderIrScene->indexEntries.size();
            summary.textureCount = parsed.blenderIrScene->textures.size();
            summary.animationCount = parsed.blenderIrScene->animations.size();
            summary.parseOk = summary.errorCount == 0U;
            summary.status = summary.parseOk ? "parsed" : "parsed-with-errors";

            const auto appendResult = combiner.appendRecordScene(std::move(*parsed.blenderIrScene), stem, record.index);
            summary.combinedMeshIndexStart = appendResult.meshIndexStart;
            summary.combinedObjectTreeIndexStart = appendResult.objectTreeIndexStart;
            summary.combinedEntryIndexStart = appendResult.entryIndexStart;
            summary.combinedTextureIndexStart = appendResult.textureIndexStart;
            summary.combinedAnimationIndexStart = appendResult.animationIndexStart;
            summary.entries = appendResult.entries;
            ++result.parsedRecordCount;
        } catch (const std::exception& ex) {
            summary.status = "skipped";
            summary.skipReason = std::string("parse-exception: ") + ex.what();
            summary.diagnosticCount = 1U;
            summary.errorCount = 1U;
            ++result.skippedRecordCount;
        }
        result.records.push_back(std::move(summary));
    }

    std::filesystem::create_directories(result.outputDir);
    writeTextFile(result.combinedBlenderIrPath, exporter.toJson(combiner.scene()));
    writeTextFile(result.metadataPath, formatMetadataJson(result));
    writeTextFile(result.recordsCsvPath, formatRecordsCsv(result));
    writeTextFile(result.manifestPath, formatManifestJson(result));
    if (!result.annotationPath.empty()) {
        const bool mediaDirExisted = std::filesystem::exists(result.annotationMediaDir);
        std::filesystem::create_directories(result.annotationMediaDir);
        result.createdAnnotationMediaDir = !mediaDirExisted && std::filesystem::exists(result.annotationMediaDir);
        std::filesystem::copy_file(
            result.combinedBlenderIrPath,
            result.annotationCombinedBlenderIrPath,
            std::filesystem::copy_options::overwrite_existing);
        result.copiedAnnotationCombinedBlenderIr = std::filesystem::exists(result.annotationCombinedBlenderIrPath);

        if (std::filesystem::exists(result.annotationPath) && !options.overwriteMlkAnnotations) {
            result.preservedExistingAnnotation = true;
        } else {
            writeTextFile(result.annotationPath, formatAnnotationJson(result, scan, path));
            result.wroteAnnotation = true;
        }
    }
    return result;
}

} // namespace

std::string generatedMldNameForKey(std::uint32_t key) {
    if (key < 10000000U) {
        const auto a = key / 100000U;
        const auto b = (key % 100000U) / 100U;
        const auto c = key % 100U;
        std::ostringstream out;
        out << 'E';
        if (a < 10U) {
            out << '0';
        }
        out << a;
        if (b < 100U) {
            out << '0';
        }
        if (b < 10U) {
            out << '0';
        }
        out << b;
        if (c < 10U) {
            out << '0';
        }
        out << c << ".MLD";
        return out.str();
    }

    const auto suffix = key - 10000000U;
    auto bucket = suffix / 1000U;
    if (bucket > 2U) {
        bucket = 0U;
    }
    constexpr char kLetters[] = { 'A', 'B', 'C' };
    std::ostringstream out;
    out << 'M' << kLetters[bucket];
    const auto number = suffix - (bucket * 1000U);
    if (number < 100U) {
        out << '0';
    }
    if (number < 10U) {
        out << '0';
    }
    out << number << ".MLD";
    return out.str();
}

MlkBlenderIrExportResult exportMlkBlenderIr(
    const std::filesystem::path& inputPath,
    const std::filesystem::path& outputDir) {
    return exportMlkBlenderIr(inputPath, outputDir, MlkBlenderIrExportOptions{});
}

MlkBlenderIrExportResult exportMlkBlenderIr(
    const std::filesystem::path& inputPath,
    const std::filesystem::path& outputDir,
    const MlkBlenderIrExportOptions& options) {
    MlkBlenderIrExportResult result{};
    result.inputPath = inputPath.string();
    const auto paths = collectMlkPaths(inputPath, result.inputWasDirectory);
    result.files.reserve(paths.size());
    for (const auto& path : paths) {
        result.files.push_back(exportFile(path, inputPath, result.inputWasDirectory, outputDir, options));
    }
    return result;
}

} // namespace spice::mlk
