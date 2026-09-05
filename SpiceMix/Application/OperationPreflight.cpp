#include "OperationPreflight.h"

#include <algorithm>
#include <cctype>
#include <filesystem>

namespace spice::mix::preflight {

template <class... Visitors>
struct Overloaded : Visitors... {
    using Visitors::operator()...;
};

bool pathIsFile(const std::filesystem::path& path) {
    std::error_code ec{};
    return std::filesystem::is_regular_file(path, ec) && !ec;
}

bool pathIsDirectory(const std::filesystem::path& path) {
    std::error_code ec{};
    return std::filesystem::is_directory(path, ec) && !ec;
}

std::string lowercase(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char character) {
        return static_cast<char>(std::tolower(character));
    });
    return value;
}

std::optional<std::string> validate(const OperationRequest& request) {
    const auto outputPath = std::visit(Overloaded{
        [](const ParseMldRequest& value) { return value.paths.output; },
        [](const ExportMldEntryListRequest& value) { return value.paths.output; },
        [](const InventoryMldGvrFormatsRequest& value) { return value.paths.output; },
        [](const ParseSctRequest& value) { return value.paths.output; },
        [](const ExportSctRequest& value) { return value.paths.output; },
        [](const ExportContentGraphRequest& value) { return value.paths.output; },
        [](const auto& value) { return value.output; },
    }, request);
    if (outputPath.empty()) {
        return "output path is required";
    }

    return std::visit(Overloaded{
        [&](const spice::mix::ParseMldRequest& value) -> std::optional<std::string> {
            return pathIsDirectory(value.paths.input) ? std::nullopt : std::optional("input directory not found: " + value.paths.input.string());
        },
        [&](const spice::mix::ExportMldEntryListRequest& value) -> std::optional<std::string> {
            return pathIsDirectory(value.paths.input) ? std::nullopt : std::optional("input directory not found: " + value.paths.input.string());
        },
        [&](const spice::mix::InventoryMldGvrFormatsRequest& value) -> std::optional<std::string> {
            return pathIsDirectory(value.paths.input) ? std::nullopt : std::optional("input directory not found: " + value.paths.input.string());
        },
        [&](const spice::mix::ParseSctRequest& value) -> std::optional<std::string> {
            return pathIsDirectory(value.paths.input) ? std::nullopt : std::optional("input directory not found: " + value.paths.input.string());
        },
        [&](const spice::mix::ExportSctRequest& value) -> std::optional<std::string> {
            return pathIsDirectory(value.paths.input) ? std::nullopt : std::optional("input directory not found: " + value.paths.input.string());
        },
        [&](const spice::mix::ExportContentGraphRequest& value) -> std::optional<std::string> {
            return pathIsDirectory(value.paths.input) ? std::nullopt : std::optional("input directory not found: " + value.paths.input.string());
        },
        [&](const spice::mix::ExportSmlResearchRequest& value) -> std::optional<std::string> {
            if (!pathIsDirectory(value.input)) return "input directory not found: " + value.input.string();
            if (!pathIsDirectory(value.annotationRepository)) return "annotation repository not found: " + value.annotationRepository.string();
            return std::nullopt;
        },
        [&](const spice::mix::ExportStdJsonRequest& value) -> std::optional<std::string> {
            return pathIsDirectory(value.input) ? std::nullopt : std::optional("input directory not found: " + value.input.string());
        },
        [&](const spice::mix::ExportMlkCorpusRequest& value) -> std::optional<std::string> {
            return std::filesystem::exists(value.input) ? std::nullopt : std::optional("input not found: " + value.input.string());
        },
        [&](const spice::mix::ExportMlkBlenderIrRequest& value) -> std::optional<std::string> {
            if (!std::filesystem::exists(value.input)) return "input not found: " + value.input.string();
            if (!pathIsDirectory(value.annotationRepository)) return "annotation repository not found: " + value.annotationRepository.string();
            return std::nullopt;
        },
        [&](const spice::mix::ReplaceMldTextureRequest& value) -> std::optional<std::string> {
            if (!pathIsFile(value.source)) return "source MLD not found: " + value.source.string();
            if (!pathIsFile(value.replacement)) return "replacement PNG not found: " + value.replacement.string();
            return std::nullopt;
        },
        [&](const spice::mix::ExtractMldTextureGvrRequest& value) -> std::optional<std::string> {
            return pathIsFile(value.input) ? std::nullopt : std::optional("input MLD not found: " + value.input.string());
        },
        [&](const spice::mix::ExtractMldTexturePngRequest& value) -> std::optional<std::string> {
            return pathIsFile(value.input) ? std::nullopt : std::optional("input MLD not found: " + value.input.string());
        },
        [&](const spice::mix::ExportAlxEnemyEventsRequest& value) -> std::optional<std::string> {
            return pathIsFile(value.input) ? std::nullopt : std::optional("input CSV not found: " + value.input.string());
        },
        [&](const spice::mix::AuditDreamcastParityRequest& value) -> std::optional<std::string> {
            if (!pathIsDirectory(value.dreamcastUs)) return "Dreamcast US corpus directory not found: " + value.dreamcastUs.string();
            if (!pathIsDirectory(value.gameCubeUs)) return "GameCube US corpus directory not found: " + value.gameCubeUs.string();
            const auto euCount = static_cast<unsigned>(value.dreamcastEuDisc1.has_value())
                + static_cast<unsigned>(value.dreamcastEuDisc2.has_value())
                + static_cast<unsigned>(value.gameCubeEu.has_value());
            if (euCount != 0U && euCount != 3U) return "Dreamcast EU disc 1, Dreamcast EU disc 2, and GameCube EU roots must be supplied together";
            if (value.dreamcastEuDisc1.has_value() && !pathIsDirectory(*value.dreamcastEuDisc1)) return "Dreamcast EU disc 1 corpus directory not found: " + value.dreamcastEuDisc1->string();
            if (value.dreamcastEuDisc2.has_value() && !pathIsDirectory(*value.dreamcastEuDisc2)) return "Dreamcast EU disc 2 corpus directory not found: " + value.dreamcastEuDisc2->string();
            if (value.gameCubeEu.has_value() && !pathIsDirectory(*value.gameCubeEu)) return "GameCube EU corpus directory not found: " + value.gameCubeEu->string();
            return std::nullopt;
        },
        [&](const spice::mix::CreateGvrRequest& value) -> std::optional<std::string> {
            return pathIsFile(value.input) ? std::nullopt : std::optional("input PNG not found: " + value.input.string());
        },
        [&](const spice::mix::ReplaceGvrRequest& value) -> std::optional<std::string> {
            if (!pathIsFile(value.source)) return "source GVR not found: " + value.source.string();
            if (!pathIsFile(value.replacement)) return "replacement PNG not found: " + value.replacement.string();
            return std::nullopt;
        },
        [&](const spice::mix::GvrToPngRequest& value) -> std::optional<std::string> {
            return pathIsFile(value.input) ? std::nullopt : std::optional("input GVR not found: " + value.input.string());
        },
        [&](const spice::mix::CreateGvrBatchRequest& value) -> std::optional<std::string> {
            return pathIsDirectory(value.input) ? std::nullopt : std::optional("input directory not found: " + value.input.string());
        },
        [&](const spice::mix::ReplaceGvrBatchRequest& value) -> std::optional<std::string> {
            if (!pathIsDirectory(value.input)) return "input directory not found: " + value.input.string();
            if (!pathIsDirectory(value.sourceGvrDirectory)) return "source GVR directory not found: " + value.sourceGvrDirectory.string();
            std::error_code ec{};
            for (std::filesystem::directory_iterator iterator(value.input, ec), end; iterator != end && !ec; iterator.increment(ec)) {
                const auto& entry = *iterator;
                if (!entry.is_regular_file(ec) || ec || lowercase(entry.path().extension().string()) != ".png") {
                    ec.clear();
                    continue;
                }
                const auto source = value.sourceGvrDirectory / (entry.path().stem().string() + ".gvr");
                if (!pathIsFile(source)) {
                    return "source GVR not found for replacement PNG: " + source.string();
                }
            }
            if (ec) return "failed to enumerate input directory: " + value.input.string();
            return std::nullopt;
        },
        [&](const spice::mix::ExportGvrImageIrRequest& value) -> std::optional<std::string> {
            return pathIsDirectory(value.input) ? std::nullopt : std::optional("input directory not found: " + value.input.string());
        },
        [&](const spice::mix::ImportGvrImageIrRequest& value) -> std::optional<std::string> {
            return pathIsDirectory(value.input) ? std::nullopt : std::optional("input directory not found: " + value.input.string());
        },
        [&](const spice::mix::CompressAklzRequest& value) -> std::optional<std::string> {
            return pathIsFile(value.input) ? std::nullopt : std::optional("input file not found: " + value.input.string());
        },
        [&](const spice::mix::DecompressAklzRequest& value) -> std::optional<std::string> {
            return pathIsFile(value.input) ? std::nullopt : std::optional("input file not found: " + value.input.string());
        },
    }, request);
}



} // namespace spice::mix::preflight
