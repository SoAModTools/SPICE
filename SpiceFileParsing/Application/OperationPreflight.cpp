#include "OperationPreflight.h"

#include <algorithm>
#include <cctype>
#include <filesystem>

namespace spice::fileparsing::preflight {

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
        [](const CompareMldSa3dRequest& value) { return value.paths.output; },
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
        [&](const spice::fileparsing::ParseMldRequest& value) -> std::optional<std::string> {
            return pathIsDirectory(value.paths.input) ? std::nullopt : std::optional("input directory not found: " + value.paths.input.string());
        },
        [&](const spice::fileparsing::CompareMldSa3dRequest& value) -> std::optional<std::string> {
            return pathIsDirectory(value.paths.input) ? std::nullopt : std::optional("input directory not found: " + value.paths.input.string());
        },
        [&](const spice::fileparsing::ExportMldEntryListRequest& value) -> std::optional<std::string> {
            return pathIsDirectory(value.paths.input) ? std::nullopt : std::optional("input directory not found: " + value.paths.input.string());
        },
        [&](const spice::fileparsing::InventoryMldGvrFormatsRequest& value) -> std::optional<std::string> {
            return pathIsDirectory(value.paths.input) ? std::nullopt : std::optional("input directory not found: " + value.paths.input.string());
        },
        [&](const spice::fileparsing::ParseSctRequest& value) -> std::optional<std::string> {
            return pathIsDirectory(value.paths.input) ? std::nullopt : std::optional("input directory not found: " + value.paths.input.string());
        },
        [&](const spice::fileparsing::ExportSctRequest& value) -> std::optional<std::string> {
            return pathIsDirectory(value.paths.input) ? std::nullopt : std::optional("input directory not found: " + value.paths.input.string());
        },
        [&](const spice::fileparsing::ExportContentGraphRequest& value) -> std::optional<std::string> {
            return pathIsDirectory(value.paths.input) ? std::nullopt : std::optional("input directory not found: " + value.paths.input.string());
        },
        [&](const spice::fileparsing::ExportSmlResearchRequest& value) -> std::optional<std::string> {
            if (!pathIsDirectory(value.input)) return "input directory not found: " + value.input.string();
            if (!pathIsDirectory(value.annotationRepository)) return "annotation repository not found: " + value.annotationRepository.string();
            return std::nullopt;
        },
        [&](const spice::fileparsing::ExportStdJsonRequest& value) -> std::optional<std::string> {
            return pathIsDirectory(value.input) ? std::nullopt : std::optional("input directory not found: " + value.input.string());
        },
        [&](const spice::fileparsing::ExportMlkCorpusRequest& value) -> std::optional<std::string> {
            return std::filesystem::exists(value.input) ? std::nullopt : std::optional("input not found: " + value.input.string());
        },
        [&](const spice::fileparsing::ExportMlkBlenderIrRequest& value) -> std::optional<std::string> {
            if (!std::filesystem::exists(value.input)) return "input not found: " + value.input.string();
            if (!pathIsDirectory(value.annotationRepository)) return "annotation repository not found: " + value.annotationRepository.string();
            return std::nullopt;
        },
        [&](const spice::fileparsing::ReplaceMldTextureRequest& value) -> std::optional<std::string> {
            if (!pathIsFile(value.source)) return "source MLD not found: " + value.source.string();
            if (!pathIsFile(value.replacement)) return "replacement PNG not found: " + value.replacement.string();
            return std::nullopt;
        },
        [&](const spice::fileparsing::ExtractMldTextureGvrRequest& value) -> std::optional<std::string> {
            return pathIsFile(value.input) ? std::nullopt : std::optional("input MLD not found: " + value.input.string());
        },
        [&](const spice::fileparsing::ExtractMldTexturePngRequest& value) -> std::optional<std::string> {
            return pathIsFile(value.input) ? std::nullopt : std::optional("input MLD not found: " + value.input.string());
        },
        [&](const spice::fileparsing::ExportAlxEnemyEventsRequest& value) -> std::optional<std::string> {
            return pathIsFile(value.input) ? std::nullopt : std::optional("input CSV not found: " + value.input.string());
        },
        [&](const spice::fileparsing::CreateGvrRequest& value) -> std::optional<std::string> {
            return pathIsFile(value.input) ? std::nullopt : std::optional("input PNG not found: " + value.input.string());
        },
        [&](const spice::fileparsing::ReplaceGvrRequest& value) -> std::optional<std::string> {
            if (!pathIsFile(value.source)) return "source GVR not found: " + value.source.string();
            if (!pathIsFile(value.replacement)) return "replacement PNG not found: " + value.replacement.string();
            return std::nullopt;
        },
        [&](const spice::fileparsing::GvrToPngRequest& value) -> std::optional<std::string> {
            return pathIsFile(value.input) ? std::nullopt : std::optional("input GVR not found: " + value.input.string());
        },
        [&](const spice::fileparsing::CreateGvrBatchRequest& value) -> std::optional<std::string> {
            return pathIsDirectory(value.input) ? std::nullopt : std::optional("input directory not found: " + value.input.string());
        },
        [&](const spice::fileparsing::ReplaceGvrBatchRequest& value) -> std::optional<std::string> {
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
        [&](const spice::fileparsing::ExportGvrImageIrRequest& value) -> std::optional<std::string> {
            return pathIsDirectory(value.input) ? std::nullopt : std::optional("input directory not found: " + value.input.string());
        },
        [&](const spice::fileparsing::ImportGvrImageIrRequest& value) -> std::optional<std::string> {
            return pathIsDirectory(value.input) ? std::nullopt : std::optional("input directory not found: " + value.input.string());
        },
        [&](const spice::fileparsing::CompressAklzRequest& value) -> std::optional<std::string> {
            return pathIsFile(value.input) ? std::nullopt : std::optional("input file not found: " + value.input.string());
        },
        [&](const spice::fileparsing::DecompressAklzRequest& value) -> std::optional<std::string> {
            return pathIsFile(value.input) ? std::nullopt : std::optional("input file not found: " + value.input.string());
        },
    }, request);
}



} // namespace spice::fileparsing::preflight
