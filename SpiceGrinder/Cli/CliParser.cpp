#include "CliParser.h"

#include <algorithm>
#include <charconv>
#include <filesystem>
#include <sstream>
#include <unordered_set>

namespace spice::grinder::cli {
namespace {

using namespace spice::mix;
using Path = std::filesystem::path;

ParseResult error(std::string message) {
    return { ParseDisposition::Error, std::nullopt, std::move(message) };
}

ParseResult run(OperationRequest request) {
    return { ParseDisposition::Run, std::move(request), {} };
}

class OptionReader {
public:
    explicit OptionReader(std::span<const std::string_view> arguments)
        : arguments_(arguments), used_(arguments.size(), false) {
    }

    std::optional<std::string> value(std::string_view name) {
        std::optional<std::string> result{};
        for (std::size_t i = 0; i < arguments_.size(); ++i) {
            if (arguments_[i] != name) {
                continue;
            }
            if (used_[i]) {
                continue;
            }
            if (result.has_value()) {
                setError("duplicate option: " + std::string(name));
                return std::nullopt;
            }
            if (i + 1 >= arguments_.size() || arguments_[i + 1].starts_with("--")) {
                setError("option requires a value: " + std::string(name));
                return std::nullopt;
            }
            used_[i] = true;
            used_[i + 1] = true;
            result = std::string(arguments_[i + 1]);
        }
        return result;
    }

    bool flag(std::string_view name) {
        bool found = false;
        for (std::size_t i = 0; i < arguments_.size(); ++i) {
            if (arguments_[i] != name || used_[i]) {
                continue;
            }
            if (found) {
                setError("duplicate option: " + std::string(name));
                return false;
            }
            used_[i] = true;
            found = true;
        }
        return found;
    }

    std::optional<Path> requiredPath(std::string_view name) {
        const auto parsed = value(name);
        if (!parsed.has_value() && error_.empty()) {
            setError("missing required option: " + std::string(name));
        }
        if (!parsed.has_value()) {
            return std::nullopt;
        }
        if (parsed->empty()) {
            setError("option requires a non-empty path: " + std::string(name));
            return std::nullopt;
        }
        return Path(*parsed);
    }

    std::optional<Path> optionalPath(std::string_view name) {
        const auto parsed = value(name);
        if (parsed.has_value() && parsed->empty()) {
            setError("option requires a non-empty path: " + std::string(name));
            return std::nullopt;
        }
        return parsed.has_value() ? std::optional<Path>(Path(*parsed)) : std::nullopt;
    }

    bool finish() {
        if (!error_.empty()) {
            return false;
        }
        for (std::size_t i = 0; i < arguments_.size(); ++i) {
            if (!used_[i]) {
                setError(arguments_[i].starts_with("--")
                    ? "unknown option: " + std::string(arguments_[i])
                    : "unexpected positional argument: " + std::string(arguments_[i]));
                return false;
            }
        }
        return true;
    }

    [[nodiscard]] const std::string& errorText() const {
        return error_;
    }

    void setError(std::string message) {
        if (error_.empty()) {
            error_ = std::move(message);
        }
    }

private:
    std::span<const std::string_view> arguments_{};
    std::vector<bool> used_{};
    std::string error_{};
};

template <typename Request>
bool parseDirectoryPaths(OptionReader& reader, Request& request, bool allowDecompressedOutput) {
    const auto input = reader.requiredPath("--input");
    const auto output = reader.requiredPath("--output");
    std::optional<Path> decompressedOutput{};
    if (allowDecompressedOutput) {
        decompressedOutput = reader.optionalPath("--decompressed-output");
    }
    if (!input.has_value() || !output.has_value()) {
        return false;
    }
    request.paths.input = *input;
    request.paths.output = *output;
    request.paths.decompressedOutput = std::move(decompressedOutput);
    return true;
}

std::optional<GvrTextureFormat> parseTextureFormat(std::string_view value) {
    if (value == "i4") return GvrTextureFormat::I4;
    if (value == "i8") return GvrTextureFormat::I8;
    if (value == "ia4") return GvrTextureFormat::IA4;
    if (value == "ia8") return GvrTextureFormat::IA8;
    if (value == "rgb565") return GvrTextureFormat::RGB565;
    if (value == "rgb5a3") return GvrTextureFormat::RGB5A3;
    if (value == "rgba8") return GvrTextureFormat::RGBA8;
    if (value == "ci4") return GvrTextureFormat::CI4;
    if (value == "ci8") return GvrTextureFormat::CI8;
    if (value == "ci14x2") return GvrTextureFormat::CI14X2;
    if (value == "cmpr") return GvrTextureFormat::CMPR;
    return std::nullopt;
}

std::optional<GvrPaletteFormat> parsePaletteFormat(std::string_view value) {
    if (value == "ia8") return GvrPaletteFormat::IA8;
    if (value == "rgb565") return GvrPaletteFormat::RGB565;
    if (value == "rgb5a3") return GvrPaletteFormat::RGB5A3;
    return std::nullopt;
}

std::optional<bool> parseOnOff(std::string_view value) {
    if (value == "on") return true;
    if (value == "off") return false;
    return std::nullopt;
}

std::optional<AklzPolicy> parseAklz(std::string_view value) {
    if (value == "preserve") return AklzPolicy::Preserve;
    if (value == "raw") return AklzPolicy::Raw;
    if (value == "compressed") return AklzPolicy::Compressed;
    return std::nullopt;
}

std::optional<std::uint32_t> parseU32(std::string_view value) {
    std::uint32_t parsed = 0;
    const int base = value.starts_with("0x") || value.starts_with("0X") ? 16 : 10;
    if (base == 16) {
        value.remove_prefix(2);
    }
    const auto result = std::from_chars(value.data(), value.data() + value.size(), parsed, base);
    if (value.empty() || result.ec != std::errc{} || result.ptr != value.data() + value.size()) {
        return std::nullopt;
    }
    return parsed;
}

bool parseEncodingSettings(
    OptionReader& reader,
    GvrEncodingSettings& settings,
    bool allowPreserve,
    AklzPolicy defaultAklz,
    GvrGlobalIndexKind defaultGlobalIndex) {
    settings.aklz = defaultAklz;
    settings.globalIndex.kind = defaultGlobalIndex;

    if (const auto value = reader.value("--format")) {
        if (*value == "preserve") {
            if (!allowPreserve) {
                reader.setError("--format preserve is not valid for creation commands");
                return false;
            }
            settings.preserveFormat = true;
        } else if (const auto parsed = parseTextureFormat(*value)) {
            settings.format = *parsed;
        } else {
            reader.setError("unknown texture format: " + *value);
            return false;
        }
    } else if (allowPreserve) {
        settings.preserveFormat = true;
    }

    if (const auto value = reader.value("--palette-format")) {
        if (*value == "preserve") {
            if (!allowPreserve) {
                reader.setError("--palette-format preserve is not valid for creation commands");
                return false;
            }
            settings.preservePaletteFormat = true;
        } else if (const auto parsed = parsePaletteFormat(*value)) {
            settings.paletteFormat = *parsed;
        } else {
            reader.setError("unknown palette format: " + *value);
            return false;
        }
    } else if (allowPreserve) {
        settings.preservePaletteFormat = true;
    }

    if (const auto value = reader.value("--mipmaps")) {
        if (*value == "preserve") {
            if (!allowPreserve) {
                reader.setError("--mipmaps preserve is not valid for creation commands");
                return false;
            }
            settings.preserveMipmaps = true;
        } else if (const auto parsed = parseOnOff(*value)) {
            settings.mipmaps = *parsed;
        } else {
            reader.setError("--mipmaps requires preserve, on, or off");
            return false;
        }
    } else if (allowPreserve) {
        settings.preserveMipmaps = true;
    }

    if (const auto value = reader.value("--aklz")) {
        const auto parsed = parseAklz(*value);
        if (!parsed.has_value() || (!allowPreserve && *parsed == AklzPolicy::Preserve)) {
            reader.setError("--aklz requires " + std::string(allowPreserve ? "preserve, raw, or compressed" : "raw or compressed"));
            return false;
        }
        settings.aklz = *parsed;
    }

    if (const auto value = reader.value("--global-index")) {
        if (*value == "preserve") {
            if (!allowPreserve) {
                reader.setError("--global-index preserve is not valid for creation commands");
                return false;
            }
            settings.globalIndex.kind = GvrGlobalIndexKind::Preserve;
        } else if (*value == "none") {
            settings.globalIndex.kind = GvrGlobalIndexKind::None;
        } else if (const auto parsed = parseU32(*value)) {
            settings.globalIndex.kind = GvrGlobalIndexKind::Value;
            settings.globalIndex.value = *parsed;
        } else {
            reader.setError("--global-index requires preserve, none, or an unsigned integer");
            return false;
        }
    }
    return true;
}

std::optional<MldTextureSelector> parseTextureSelector(OptionReader& reader) {
    const auto indexText = reader.value("--texture-index");
    const auto name = reader.value("--texture-name");
    if (indexText.has_value() == name.has_value()) {
        reader.setError("exactly one of --texture-index or --texture-name is required");
        return std::nullopt;
    }
    if (indexText.has_value()) {
        const auto index = parseU32(*indexText);
        if (!index.has_value()) {
            reader.setError("--texture-index requires an unsigned integer");
            return std::nullopt;
        }
        return TextureIndex{ static_cast<std::size_t>(*index) };
    }
    if (name->empty()) {
        reader.setError("--texture-name cannot be empty");
        return std::nullopt;
    }
    return TextureName{ *name };
}

std::string commandHelp(std::string_view command) {
    const std::string prefix = "Usage:\n  SpiceGrinder " + std::string(command) + " ";
    if (command == "parse-mld") return prefix + "--input <dir> --output <dir> [--extract-grnd-gobj-blocks] [--decompressed-output <dir>]\n";
    if (command == "compare-mld-sa3d") return prefix + "--input <dir> --output <dir> [--extract-grnd-gobj-blocks] [--decompressed-output <dir>]\n";
    if (command == "export-mld-entry-list") return prefix + "--input <dir> --output <dir> [--decompressed-output <dir>]\n";
    if (command == "inventory-mld-gvr-formats") return prefix + "--input <dir> --output <dir> [--decompressed-output <dir>]\n";
    if (command == "replace-mld-texture") return prefix + "--source <mld> --replacement <png> --output <mld> (--texture-index <n>|--texture-name <name>) [encoding options]\n";
    if (command == "extract-mld-texture-gvr") return prefix + "--input <mld> --output <gvr> (--texture-index <n>|--texture-name <name>)\n";
    if (command == "extract-mld-texture-png") return prefix + "--input <mld> --output <png> (--texture-index <n>|--texture-name <name>) [--gvr-output <gvr>]\n";
    if (command == "parse-sct") return prefix + "--input <dir> --output <dir> [--decode-unreached-code] [--decompressed-output <dir>]\n";
    if (command == "export-sct") return prefix + "--input <dir> --output <dir> [--compression raw|aklz] [--decode-unreached-code] [--decompressed-output <dir>]\n";
    if (command == "export-sml-research") return prefix + "--input <dir> --output <dir> --annotation-repository <dir> [output selectors]\n";
    if (command == "export-std-json") return prefix + "--input <dir> --output <dir>\n";
    if (command == "export-mlk-corpus") return prefix + "--input <file-or-dir> --output <dir>\n";
    if (command == "export-mlk-blender-ir") return prefix + "--input <file-or-dir> --output <dir> --annotation-repository <dir> [--overwrite-annotations]\n";
    if (command == "export-content-graph") return prefix + "--input <dir> --output <dir> [--projection full|sections|world] [--decompressed-output <dir>]\n";
    if (command == "export-alx-enemy-events") return prefix + "--input <csv> --output <json>\n";
    if (command == "audit-dreamcast-parity") return prefix + "--dreamcast-us <dir> --gamecube-us <dir> --output <dir> [--dreamcast-eu-disc1 <dir> --dreamcast-eu-disc2 <dir> --gamecube-eu <dir>]\n";
    if (command == "create-gvr") return prefix + "--input <png> --output <gvr> [encoding options]\n";
    if (command == "replace-gvr") return prefix + "--source <gvr> --input <png> --output <gvr> [encoding options]\n";
    if (command == "gvr-to-png") return prefix + "--input <gvr> --output <png>\n";
    if (command == "create-gvr-batch") return prefix + "--input <png-dir> --output <gvr-dir> [encoding options]\n";
    if (command == "replace-gvr-batch") return prefix + "--input <png-dir> --source-gvr-dir <dir> --output <gvr-dir> [encoding options]\n";
    if (command == "export-gvr-image-ir") return prefix + "--input <gvr-dir> --output <dir>\n";
    if (command == "import-gvr-image-ir") return prefix + "--input <ir-dir> --output <dir> [--aklz preserve|raw|compressed]\n";
    if (command == "compress-aklz" || command == "decompress-aklz") return prefix + "--input <file> --output <file>\n";
    return {};
}

const std::unordered_set<std::string_view>& commands() {
    static const std::unordered_set<std::string_view> result{
        "parse-mld", "compare-mld-sa3d", "export-mld-entry-list", "inventory-mld-gvr-formats",
        "replace-mld-texture", "extract-mld-texture-gvr", "extract-mld-texture-png",
        "parse-sct", "export-sct", "export-sml-research", "export-std-json", "export-mlk-corpus",
        "export-mlk-blender-ir", "export-content-graph", "export-alx-enemy-events", "create-gvr",
        "replace-gvr", "gvr-to-png", "create-gvr-batch", "replace-gvr-batch", "export-gvr-image-ir",
        "import-gvr-image-ir", "compress-aklz", "decompress-aklz", "audit-dreamcast-parity"
    };
    return result;
}

} // namespace

std::string globalHelp() {
    return
        "Usage:\n"
        "  SpiceGrinder <command> [options]\n\n"
        "Commands:\n"
        "  parse-mld                    Parse MLD files and export summaries/Blender IR.\n"
        "  compare-mld-sa3d             Compare MLD parsing with the SA3D reference bridge.\n"
        "  export-mld-entry-list        Export compact MLD entry lists.\n"
        "  inventory-mld-gvr-formats   Inventory embedded GVR formats in MLD files.\n"
        "  replace-mld-texture          Replace one embedded MLD texture.\n"
        "  extract-mld-texture-gvr      Extract one embedded texture as GVR.\n"
        "  extract-mld-texture-png      Extract one embedded texture as PNG.\n"
        "  parse-sct                    Parse SCT files.\n"
        "  export-sct                   Export canonical SCT files and round-trip reports.\n"
        "  export-sml-research          Export SML/SST research artifacts.\n"
        "  export-std-json              Export STD files as JSON.\n"
        "  export-mlk-corpus            Export MLK corpus reports.\n"
        "  export-mlk-blender-ir        Export MLK Blender IR contact sheets.\n"
        "  export-content-graph         Export a combined MLD/SCT content graph.\n"
        "  export-alx-enemy-events      Export enemy-event CSV as JSON.\n"
        "  audit-dreamcast-parity       Compare Dreamcast and GameCube parser semantics.\n"
        "  create-gvr                   Create one GVR from PNG.\n"
        "  replace-gvr                  Replace one GVR from PNG.\n"
        "  gvr-to-png                   Decode one GVR to PNG.\n"
        "  create-gvr-batch             Create GVRs for a PNG directory.\n"
        "  replace-gvr-batch            Replace GVRs for a PNG directory.\n"
        "  export-gvr-image-ir          Export GVR PNG/sidecar IR.\n"
        "  import-gvr-image-ir          Import GVR PNG/sidecar IR.\n"
        "  compress-aklz                Compress one file with AKLZ.\n"
        "  decompress-aklz              Decompress one AKLZ file.\n\n"
        "Use 'SpiceGrinder <command> --help' for command-specific usage.\n";
}

ParseResult parse(std::span<const std::string_view> arguments) {
    if (arguments.empty() || (arguments.size() == 1 && arguments.front() == "--help")) {
        return { ParseDisposition::Help, std::nullopt, globalHelp() };
    }

    const auto command = arguments.front();
    if (!commands().contains(command)) {
        return error("unknown command: " + std::string(command));
    }
    if (arguments.size() == 2 && arguments[1] == "--help") {
        return { ParseDisposition::Help, std::nullopt, commandHelp(command) };
    }

    OptionReader reader(arguments.subspan(1));

    if (command == "parse-mld") {
        ParseMldRequest request{};
        parseDirectoryPaths(reader, request, true);
        request.extractGrndGobjBlocks = reader.flag("--extract-grnd-gobj-blocks");
        return reader.finish() ? run(request) : error(reader.errorText());
    }
    if (command == "compare-mld-sa3d") {
        CompareMldSa3dRequest request{};
        parseDirectoryPaths(reader, request, true);
        request.extractGrndGobjBlocks = reader.flag("--extract-grnd-gobj-blocks");
        return reader.finish() ? run(request) : error(reader.errorText());
    }
    if (command == "export-mld-entry-list") {
        ExportMldEntryListRequest request{};
        parseDirectoryPaths(reader, request, true);
        return reader.finish() ? run(request) : error(reader.errorText());
    }
    if (command == "inventory-mld-gvr-formats") {
        InventoryMldGvrFormatsRequest request{};
        parseDirectoryPaths(reader, request, true);
        return reader.finish() ? run(request) : error(reader.errorText());
    }
    if (command == "parse-sct") {
        ParseSctRequest request{};
        parseDirectoryPaths(reader, request, true);
        request.decodeUnreachedCode = reader.flag("--decode-unreached-code");
        return reader.finish() ? run(request) : error(reader.errorText());
    }
    if (command == "export-sct") {
        ExportSctRequest request{};
        parseDirectoryPaths(reader, request, true);
        request.decodeUnreachedCode = reader.flag("--decode-unreached-code");
        if (const auto compression = reader.value("--compression")) {
            if (*compression == "raw") request.compressAklz = false;
            else if (*compression == "aklz") request.compressAklz = true;
            else reader.setError("--compression requires raw or aklz");
        }
        return reader.finish() ? run(request) : error(reader.errorText());
    }
    if (command == "export-content-graph") {
        ExportContentGraphRequest request{};
        parseDirectoryPaths(reader, request, true);
        if (const auto projection = reader.value("--projection")) {
            if (*projection == "full") request.projection = ContentGraphProjection::Full;
            else if (*projection == "sections") request.projection = ContentGraphProjection::Sections;
            else if (*projection == "world") request.projection = ContentGraphProjection::World;
            else reader.setError("--projection requires full, sections, or world");
        }
        return reader.finish() ? run(request) : error(reader.errorText());
    }
    if (command == "replace-mld-texture") {
        ReplaceMldTextureRequest request{};
        const auto source = reader.requiredPath("--source");
        const auto replacement = reader.requiredPath("--replacement");
        const auto output = reader.requiredPath("--output");
        const auto selector = parseTextureSelector(reader);
        parseEncodingSettings(reader, request.encoding, true, AklzPolicy::Preserve, GvrGlobalIndexKind::Preserve);
        request.allowDimensionChange = reader.flag("--allow-dimension-change");
        request.allowPostArchiveShift = reader.flag("--allow-post-archive-shift");
        if (source) request.source = *source;
        if (replacement) request.replacement = *replacement;
        if (output) request.output = *output;
        if (selector) request.selector = *selector;
        return reader.finish() && source && replacement && output && selector ? run(request) : error(reader.errorText());
    }
    if (command == "extract-mld-texture-gvr" || command == "extract-mld-texture-png") {
        const auto input = reader.requiredPath("--input");
        const auto output = reader.requiredPath("--output");
        const auto selector = parseTextureSelector(reader);
        if (command == "extract-mld-texture-gvr") {
            ExtractMldTextureGvrRequest request{};
            if (input) request.input = *input;
            if (output) request.output = *output;
            if (selector) request.selector = *selector;
            return reader.finish() && input && output && selector ? run(request) : error(reader.errorText());
        }
        ExtractMldTexturePngRequest request{};
        request.gvrOutput = reader.optionalPath("--gvr-output");
        if (input) request.input = *input;
        if (output) request.output = *output;
        if (selector) request.selector = *selector;
        return reader.finish() && input && output && selector ? run(request) : error(reader.errorText());
    }
    if (command == "export-sml-research") {
        ExportSmlResearchRequest request{};
        const auto input = reader.requiredPath("--input");
        const auto output = reader.requiredPath("--output");
        const auto annotations = reader.requiredPath("--annotation-repository");
        request.embeddedMld = reader.flag("--embedded-mld");
        request.embeddedMldBlenderIr = reader.flag("--embedded-mld-blender-ir");
        request.combinedBlenderIr = reader.flag("--combined-blender-ir");
        request.commandMap = reader.flag("--command-map");
        if (request.embeddedMldBlenderIr) request.embeddedMld = true;
        if (const auto placement = reader.value("--combined-placement")) {
            if (*placement == "sst") request.combinedPlacement = CombinedPlacement::Sst;
            else if (*placement == "raw") request.combinedPlacement = CombinedPlacement::Raw;
            else reader.setError("--combined-placement requires sst or raw");
            if (!request.combinedBlenderIr) reader.setError("--combined-placement requires --combined-blender-ir");
        }
        if (!request.embeddedMld && !request.combinedBlenderIr && !request.commandMap) {
            reader.setError("at least one SML output selector is required");
        }
        if (input) request.input = *input;
        if (output) request.output = *output;
        if (annotations) request.annotationRepository = *annotations;
        return reader.finish() && input && output && annotations ? run(request) : error(reader.errorText());
    }
    if (command == "export-std-json" || command == "export-mlk-corpus") {
        const auto input = reader.requiredPath("--input");
        const auto output = reader.requiredPath("--output");
        if (command == "export-std-json") {
            ExportStdJsonRequest request{};
            if (input) request.input = *input;
            if (output) request.output = *output;
            return reader.finish() && input && output ? run(request) : error(reader.errorText());
        }
        ExportMlkCorpusRequest request{};
        if (input) request.input = *input;
        if (output) request.output = *output;
        return reader.finish() && input && output ? run(request) : error(reader.errorText());
    }
    if (command == "export-mlk-blender-ir") {
        ExportMlkBlenderIrRequest request{};
        const auto input = reader.requiredPath("--input");
        const auto output = reader.requiredPath("--output");
        const auto annotations = reader.requiredPath("--annotation-repository");
        request.overwriteAnnotations = reader.flag("--overwrite-annotations");
        if (input) request.input = *input;
        if (output) request.output = *output;
        if (annotations) request.annotationRepository = *annotations;
        return reader.finish() && input && output && annotations ? run(request) : error(reader.errorText());
    }
    if (command == "export-alx-enemy-events" || command == "gvr-to-png" || command == "compress-aklz" || command == "decompress-aklz") {
        const auto input = reader.requiredPath("--input");
        const auto output = reader.requiredPath("--output");
        if (!reader.finish() || !input || !output) return error(reader.errorText());
        if (command == "export-alx-enemy-events") return run(ExportAlxEnemyEventsRequest{ *input, *output });
        if (command == "gvr-to-png") return run(GvrToPngRequest{ *input, *output });
        if (command == "compress-aklz") return run(CompressAklzRequest{ *input, *output });
        return run(DecompressAklzRequest{ *input, *output });
    }
    if (command == "audit-dreamcast-parity") {
        AuditDreamcastParityRequest request{};
        const auto dreamcastUs = reader.requiredPath("--dreamcast-us");
        const auto gameCubeUs = reader.requiredPath("--gamecube-us");
        const auto output = reader.requiredPath("--output");
        request.dreamcastEuDisc1 = reader.optionalPath("--dreamcast-eu-disc1");
        request.dreamcastEuDisc2 = reader.optionalPath("--dreamcast-eu-disc2");
        request.gameCubeEu = reader.optionalPath("--gamecube-eu");
        const auto euCount = static_cast<unsigned>(request.dreamcastEuDisc1.has_value())
            + static_cast<unsigned>(request.dreamcastEuDisc2.has_value())
            + static_cast<unsigned>(request.gameCubeEu.has_value());
        if (euCount != 0U && euCount != 3U) {
            reader.setError("the three EU corpus options must be supplied together");
        }
        if (dreamcastUs) request.dreamcastUs = *dreamcastUs;
        if (gameCubeUs) request.gameCubeUs = *gameCubeUs;
        if (output) request.output = *output;
        return reader.finish() && dreamcastUs && gameCubeUs && output
            ? run(request) : error(reader.errorText());
    }
    if (command == "create-gvr" || command == "create-gvr-batch") {
        const auto input = reader.requiredPath("--input");
        const auto output = reader.requiredPath("--output");
        GvrEncodingSettings encoding{};
        parseEncodingSettings(reader, encoding, false, AklzPolicy::Raw, GvrGlobalIndexKind::None);
        if (!reader.finish() || !input || !output) return error(reader.errorText());
        if (command == "create-gvr") return run(CreateGvrRequest{ *input, *output, encoding });
        return run(CreateGvrBatchRequest{ *input, *output, encoding });
    }
    if (command == "replace-gvr") {
        const auto source = reader.requiredPath("--source");
        const auto input = reader.requiredPath("--input");
        const auto output = reader.requiredPath("--output");
        GvrEncodingSettings encoding{};
        parseEncodingSettings(reader, encoding, true, AklzPolicy::Preserve, GvrGlobalIndexKind::Preserve);
        return reader.finish() && source && input && output
            ? run(ReplaceGvrRequest{ *source, *input, *output, encoding }) : error(reader.errorText());
    }
    if (command == "replace-gvr-batch") {
        const auto input = reader.requiredPath("--input");
        const auto source = reader.requiredPath("--source-gvr-dir");
        const auto output = reader.requiredPath("--output");
        GvrEncodingSettings encoding{};
        parseEncodingSettings(reader, encoding, true, AklzPolicy::Preserve, GvrGlobalIndexKind::Preserve);
        return reader.finish() && input && source && output
            ? run(ReplaceGvrBatchRequest{ *input, *source, *output, encoding }) : error(reader.errorText());
    }
    if (command == "export-gvr-image-ir") {
        const auto input = reader.requiredPath("--input");
        const auto output = reader.requiredPath("--output");
        return reader.finish() && input && output ? run(ExportGvrImageIrRequest{ *input, *output }) : error(reader.errorText());
    }
    if (command == "import-gvr-image-ir") {
        const auto input = reader.requiredPath("--input");
        const auto output = reader.requiredPath("--output");
        AklzPolicy aklz = AklzPolicy::Preserve;
        if (const auto value = reader.value("--aklz")) {
            const auto parsed = parseAklz(*value);
            if (parsed) aklz = *parsed;
            else reader.setError("--aklz requires preserve, raw, or compressed");
        }
        return reader.finish() && input && output ? run(ImportGvrImageIrRequest{ *input, *output, aklz }) : error(reader.errorText());
    }

    return error("command parser is not implemented: " + std::string(command));
}

ParseResult parse(int argc, char** argv) {
    std::vector<std::string_view> arguments{};
    arguments.reserve(argc > 1 ? static_cast<std::size_t>(argc - 1) : 0U);
    for (int i = 1; i < argc; ++i) {
        arguments.emplace_back(argv[i]);
    }
    return parse(arguments);
}

} // namespace spice::grinder::cli
