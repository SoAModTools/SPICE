#include "MldGvrFormatInventory.h"

#include <algorithm>
#include <iomanip>
#include <map>
#include <set>
#include <sstream>

namespace spice::mld::analysis {
namespace {

constexpr std::size_t kMaxDiagnosticPreview = 3U;
constexpr std::size_t kMaxRepresentativeSamples = 5U;

[[nodiscard]] bool usesPalette(const MldGvrTextureSample& sample) {
    return sample.sourceFormat == "CI4" ||
        sample.sourceFormat == "CI8" ||
        sample.sourceFormat == "CI14X2" ||
        sample.hasInternalPalette ||
        sample.paletteDataSize > 0U;
}

[[nodiscard]] std::string hexByte(const std::uint8_t value) {
    std::ostringstream out{};
    out << "0x" << std::uppercase << std::hex << std::setw(2) << std::setfill('0')
        << static_cast<unsigned int>(value);
    return out.str();
}

[[nodiscard]] std::string jsonEscape(const std::string& value) {
    std::string out{};
    out.reserve(value.size() + 8U);
    for (const char ch : value) {
        switch (ch) {
        case '\\': out += "\\\\"; break;
        case '"': out += "\\\""; break;
        case '\b': out += "\\b"; break;
        case '\f': out += "\\f"; break;
        case '\n': out += "\\n"; break;
        case '\r': out += "\\r"; break;
        case '\t': out += "\\t"; break;
        default:
            if (static_cast<unsigned char>(ch) < 0x20U) {
                out += "\\u00";
                constexpr char digits[] = "0123456789abcdef";
                out.push_back(digits[(static_cast<unsigned char>(ch) >> 4U) & 0x0FU]);
                out.push_back(digits[static_cast<unsigned char>(ch) & 0x0FU]);
            } else {
                out.push_back(ch);
            }
            break;
        }
    }
    return out;
}

[[nodiscard]] std::string groupKey(const MldGvrTextureSample& sample) {
    std::ostringstream out{};
    out << sample.sourceFormat
        << "|" << sample.sourcePaletteFormat
        << "|" << static_cast<unsigned int>(sample.rawFlags)
        << "|" << static_cast<unsigned int>(sample.rawDataFormat)
        << "|" << (sample.hasMipmaps ? "mip" : "base")
        << "|" << (sample.hasInternalPalette ? "pal" : "no-pal");
    return out.str();
}

[[nodiscard]] bool groupSortLess(const MldGvrFormatGroup& lhs, const MldGvrFormatGroup& rhs) {
    if (lhs.encoderCovered != rhs.encoderCovered) {
        return !lhs.encoderCovered;
    }
    if (lhs.textureCount != rhs.textureCount) {
        return lhs.textureCount > rhs.textureCount;
    }
    if (lhs.sourceFiles.size() != rhs.sourceFiles.size()) {
        return lhs.sourceFiles.size() > rhs.sourceFiles.size();
    }
    if (lhs.sourceFormat != rhs.sourceFormat) {
        return lhs.sourceFormat < rhs.sourceFormat;
    }
    if (lhs.sourcePaletteFormat != rhs.sourcePaletteFormat) {
        return lhs.sourcePaletteFormat < rhs.sourcePaletteFormat;
    }
    if (lhs.rawDataFormat != rhs.rawDataFormat) {
        return lhs.rawDataFormat < rhs.rawDataFormat;
    }
    return lhs.rawFlags < rhs.rawFlags;
}

[[nodiscard]] int missingComplexityScore(const MldGvrFormatGroup& group) {
    int score = 0;
    if (group.hasMipmaps) {
        score += 2;
    }
    if (group.hasInternalPalette || group.sourcePaletteFormat != "None") {
        score += 1;
    }
    if (group.sourceFormat == "CI4" || group.sourceFormat == "CI8" || group.sourceFormat == "CI14X2") {
        score += 1;
    }
    if (group.sourceFormat == "CMPR") {
        score += 2;
    }
    return score;
}

[[nodiscard]] bool prioritySortLess(const MldGvrFormatGroup& lhs, const MldGvrFormatGroup& rhs) {
    if (lhs.textureCount != rhs.textureCount) {
        return lhs.textureCount > rhs.textureCount;
    }
    if (lhs.sourceFiles.size() != rhs.sourceFiles.size()) {
        return lhs.sourceFiles.size() > rhs.sourceFiles.size();
    }
    const auto lhsComplexity = missingComplexityScore(lhs);
    const auto rhsComplexity = missingComplexityScore(rhs);
    if (lhsComplexity != rhsComplexity) {
        return lhsComplexity < rhsComplexity;
    }
    return groupSortLess(lhs, rhs);
}

void writeJsonSample(std::ostream& out, const MldGvrTextureSample& sample, const std::string& indent) {
    out << indent << "{\n";
    out << indent << "  \"sourcePath\": \"" << jsonEscape(sample.sourcePath) << "\",\n";
    out << indent << "  \"textureIndex\": " << sample.textureIndex << ",\n";
    out << indent << "  \"archiveTextureIndex\": " << sample.archiveTextureIndex << ",\n";
    out << indent << "  \"textureName\": \"" << jsonEscape(sample.textureName) << "\",\n";
    out << indent << "  \"hasGlobalIndex\": " << (sample.hasGlobalIndex ? "true" : "false") << ",\n";
    out << indent << "  \"globalIndex\": " << sample.globalIndex << ",\n";
    out << indent << "  \"rawFlags\": \"" << hexByte(sample.rawFlags) << "\",\n";
    out << indent << "  \"rawDataFormat\": \"" << hexByte(sample.rawDataFormat) << "\",\n";
    out << indent << "  \"sourceFormat\": \"" << jsonEscape(sample.sourceFormat) << "\",\n";
    out << indent << "  \"sourcePaletteFormat\": \"" << jsonEscape(sample.sourcePaletteFormat) << "\",\n";
    out << indent << "  \"hasMipmaps\": " << (sample.hasMipmaps ? "true" : "false") << ",\n";
    out << indent << "  \"hasInternalPalette\": " << (sample.hasInternalPalette ? "true" : "false") << ",\n";
    out << indent << "  \"width\": " << sample.width << ",\n";
    out << indent << "  \"height\": " << sample.height << ",\n";
    out << indent << "  \"imageDataSize\": " << sample.imageDataSize << ",\n";
    out << indent << "  \"paletteDataSize\": " << sample.paletteDataSize << ",\n";
    out << indent << "  \"decoded\": " << (sample.decoded ? "true" : "false") << ",\n";
    out << indent << "  \"diagnosticPreview\": [";
    for (std::size_t i = 0; i < sample.diagnosticPreview.size(); ++i) {
        if (i != 0U) {
            out << ", ";
        }
        out << "\"" << jsonEscape(sample.diagnosticPreview[i]) << "\"";
    }
    out << "]\n";
    out << indent << "}";
}

void writeJsonGroup(std::ostream& out, const MldGvrFormatGroup& group, const std::string& indent) {
    out << indent << "{\n";
    out << indent << "  \"sourceFormat\": \"" << jsonEscape(group.sourceFormat) << "\",\n";
    out << indent << "  \"sourcePaletteFormat\": \"" << jsonEscape(group.sourcePaletteFormat) << "\",\n";
    out << indent << "  \"rawFlags\": \"" << hexByte(group.rawFlags) << "\",\n";
    out << indent << "  \"rawDataFormat\": \"" << hexByte(group.rawDataFormat) << "\",\n";
    out << indent << "  \"hasMipmaps\": " << (group.hasMipmaps ? "true" : "false") << ",\n";
    out << indent << "  \"hasInternalPalette\": " << (group.hasInternalPalette ? "true" : "false") << ",\n";
    out << indent << "  \"encoderCovered\": " << (group.encoderCovered ? "true" : "false") << ",\n";
    out << indent << "  \"textureCount\": " << group.textureCount << ",\n";
    out << indent << "  \"decodedCount\": " << group.decodedCount << ",\n";
    out << indent << "  \"sourceFiles\": [";
    for (std::size_t i = 0; i < group.sourceFiles.size(); ++i) {
        if (i != 0U) {
            out << ", ";
        }
        out << "\"" << jsonEscape(group.sourceFiles[i]) << "\"";
    }
    out << "],\n";
    out << indent << "  \"representativeSamples\": [\n";
    for (std::size_t i = 0; i < group.representativeSamples.size(); ++i) {
        if (i != 0U) {
            out << ",\n";
        }
        writeJsonSample(out, group.representativeSamples[i], indent + "    ");
    }
    out << "\n" << indent << "  ]\n";
    out << indent << "}";
}

[[nodiscard]] MldGvrTextureSample toSample(const std::string& sourcePath,
    const std::size_t textureIndex,
    const model::MldTextureEntry& entry) {
    MldGvrTextureSample sample{};
    sample.sourcePath = sourcePath;
    sample.textureIndex = textureIndex;
    sample.archiveTextureIndex = entry.archiveTextureIndex;
    sample.textureName = entry.textureName;
    sample.hasGlobalIndex = entry.hasGlobalIndex;
    sample.globalIndex = entry.globalIndex;
    sample.rawFlags = entry.pixelFormat;
    sample.rawDataFormat = entry.dataFormat;
    sample.sourceFormat = entry.sourceFormat;
    sample.sourcePaletteFormat = entry.sourcePaletteFormat;
    sample.hasMipmaps = entry.hasMipmaps;
    sample.hasInternalPalette = entry.hasInternalPalette;
    sample.width = entry.width;
    sample.height = entry.height;
    sample.imageDataSize = entry.imageDataSize;
    sample.paletteDataSize = entry.paletteDataSize;
    sample.decoded = entry.decoded;
    const auto previewCount = std::min(kMaxDiagnosticPreview, entry.diagnostics.size());
    sample.diagnosticPreview.assign(entry.diagnostics.begin(), entry.diagnostics.begin() + static_cast<std::ptrdiff_t>(previewCount));
    return sample;
}

} // namespace

void MldGvrFormatInventoryBuilder::noteFileScanned() {
    ++inventory_.filesScanned;
}

void MldGvrFormatInventoryBuilder::addParseFailure(const std::string& sourcePath, const std::string& message) {
    ++inventory_.filesFailed;
    inventory_.failures.push_back(sourcePath + ": " + message);
}

void MldGvrFormatInventoryBuilder::addParsedMld(const std::string& sourcePath, const model::MldTextureArchive& archive) {
    ++inventory_.filesParsed;
    for (std::size_t i = 0; i < archive.entries.size(); ++i) {
        auto sample = toSample(sourcePath, i, archive.entries[i]);
        if (sample.decoded) {
            ++inventory_.decodedTextureCount;
        }
        ++inventory_.textureCount;
        inventory_.samples.push_back(std::move(sample));
    }
}

MldGvrFormatInventory MldGvrFormatInventoryBuilder::build() const {
    auto out = inventory_;

    std::map<std::string, MldGvrFormatGroup> groups{};
    std::map<std::string, std::set<std::string>> fileSets{};
    for (const auto& sample : out.samples) {
        auto& group = groups[groupKey(sample)];
        if (group.textureCount == 0U) {
            group.sourceFormat = sample.sourceFormat;
            group.sourcePaletteFormat = sample.sourcePaletteFormat;
            group.rawFlags = sample.rawFlags;
            group.rawDataFormat = sample.rawDataFormat;
            group.hasMipmaps = sample.hasMipmaps;
            group.hasInternalPalette = sample.hasInternalPalette;
            group.encoderCovered = isGvrEncoderCovered(sample);
        }
        ++group.textureCount;
        if (sample.decoded) {
            ++group.decodedCount;
        }
        fileSets[groupKey(sample)].insert(sample.sourcePath);
        if (group.representativeSamples.size() < kMaxRepresentativeSamples) {
            group.representativeSamples.push_back(sample);
        }
    }

    out.formatGroups.clear();
    out.formatGroups.reserve(groups.size());
    for (auto& [key, group] : groups) {
        const auto filesIt = fileSets.find(key);
        if (filesIt != fileSets.end()) {
            group.sourceFiles.assign(filesIt->second.begin(), filesIt->second.end());
        }
        out.formatGroups.push_back(std::move(group));
    }
    std::sort(out.formatGroups.begin(), out.formatGroups.end(), groupSortLess);

    out.priorityGroups.clear();
    for (const auto& group : out.formatGroups) {
        if (!group.encoderCovered) {
            out.priorityGroups.push_back(group);
        }
    }
    std::sort(out.priorityGroups.begin(), out.priorityGroups.end(), prioritySortLess);

    return out;
}

bool isGvrEncoderCovered(const MldGvrTextureSample& sample) {
    if (sample.sourceFormat == "RGBA8") {
        return sample.sourcePaletteFormat == "None" &&
            !usesPalette(sample);
    }
    if (sample.sourceFormat == "RGB5A3") {
        return sample.sourcePaletteFormat == "None" &&
            !usesPalette(sample);
    }
    if (sample.sourceFormat == "CMPR") {
        return sample.sourcePaletteFormat == "None" &&
            !usesPalette(sample);
    }
    if (sample.sourceFormat == "CI4") {
        return sample.sourcePaletteFormat == "RGB5A3" &&
            sample.hasInternalPalette &&
            sample.paletteDataSize == 32U;
    }
    return false;
}

std::string formatMldGvrFormatInventoryJson(const MldGvrFormatInventory& inventory) {
    std::ostringstream out{};
    out << "{\n";
    out << "  \"schemaVersion\": 1,\n";
    out << "  \"filesScanned\": " << inventory.filesScanned << ",\n";
    out << "  \"filesParsed\": " << inventory.filesParsed << ",\n";
    out << "  \"filesFailed\": " << inventory.filesFailed << ",\n";
    out << "  \"textureCount\": " << inventory.textureCount << ",\n";
    out << "  \"decodedTextureCount\": " << inventory.decodedTextureCount << ",\n";
    out << "  \"currentEncoderCoverage\": \"RGBA8 base/mip textures, RGB5A3 base/mip textures, CMPR base/mip textures, and CI4 RGB5A3 internal-palette base/mip textures\",\n";
    out << "  \"failures\": [";
    for (std::size_t i = 0; i < inventory.failures.size(); ++i) {
        if (i != 0U) {
            out << ", ";
        }
        out << "\"" << jsonEscape(inventory.failures[i]) << "\"";
    }
    out << "],\n";
    out << "  \"formatGroups\": [\n";
    for (std::size_t i = 0; i < inventory.formatGroups.size(); ++i) {
        if (i != 0U) {
            out << ",\n";
        }
        writeJsonGroup(out, inventory.formatGroups[i], "    ");
    }
    out << "\n  ],\n";
    out << "  \"priorityGroups\": [\n";
    for (std::size_t i = 0; i < inventory.priorityGroups.size(); ++i) {
        if (i != 0U) {
            out << ",\n";
        }
        writeJsonGroup(out, inventory.priorityGroups[i], "    ");
    }
    out << "\n  ],\n";
    out << "  \"samples\": [\n";
    for (std::size_t i = 0; i < inventory.samples.size(); ++i) {
        if (i != 0U) {
            out << ",\n";
        }
        writeJsonSample(out, inventory.samples[i], "    ");
    }
    out << "\n  ]\n";
    out << "}\n";
    return out.str();
}

std::string formatMldGvrFormatInventoryMarkdown(const MldGvrFormatInventory& inventory) {
    std::ostringstream out{};
    out << "# MLD GVR Format Priority Report\n\n";
    out << "Current encoder coverage: RGBA8 base/mip textures, RGB5A3 base/mip textures, CMPR base/mip textures, and CI4 RGB5A3 internal-palette base/mip textures.\n\n";
    out << "## Summary\n\n";
    out << "- Files scanned: " << inventory.filesScanned << "\n";
    out << "- Files parsed: " << inventory.filesParsed << "\n";
    out << "- Files failed: " << inventory.filesFailed << "\n";
    out << "- Textures found: " << inventory.textureCount << "\n";
    out << "- Decoded textures: " << inventory.decodedTextureCount << "\n";
    out << "- Distinct format groups: " << inventory.formatGroups.size() << "\n";
    out << "- Unsupported encoder groups: " << inventory.priorityGroups.size() << "\n\n";

    out << "## Priority Encoder Targets\n\n";
    if (inventory.priorityGroups.empty()) {
        out << "All sampled format groups are covered by the current encoder.\n\n";
    } else {
        out << "| Rank | Format | Palette | Raw Flags | Raw Data | Mipmaps | Internal Palette | Textures | Files | Representative Samples |\n";
        out << "| ---: | --- | --- | --- | --- | --- | --- | ---: | ---: | --- |\n";
        for (std::size_t i = 0; i < inventory.priorityGroups.size(); ++i) {
            const auto& group = inventory.priorityGroups[i];
            out << "| " << (i + 1U)
                << " | " << group.sourceFormat
                << " | " << group.sourcePaletteFormat
                << " | " << hexByte(group.rawFlags)
                << " | " << hexByte(group.rawDataFormat)
                << " | " << (group.hasMipmaps ? "yes" : "no")
                << " | " << (group.hasInternalPalette ? "yes" : "no")
                << " | " << group.textureCount
                << " | " << group.sourceFiles.size()
                << " | ";
            for (std::size_t sampleIndex = 0; sampleIndex < group.representativeSamples.size(); ++sampleIndex) {
                if (sampleIndex != 0U) {
                    out << "<br>";
                }
                const auto& sample = group.representativeSamples[sampleIndex];
                out << sample.sourcePath << "#" << sample.textureIndex;
                if (!sample.textureName.empty()) {
                    out << " " << sample.textureName;
                }
            }
            out << " |\n";
        }
        out << "\n";
    }

    out << "## All Format Groups\n\n";
    out << "| Covered | Format | Palette | Raw Flags | Raw Data | Mipmaps | Internal Palette | Textures | Decoded | Files |\n";
    out << "| --- | --- | --- | --- | --- | --- | --- | ---: | ---: | ---: |\n";
    for (const auto& group : inventory.formatGroups) {
        out << "| " << (group.encoderCovered ? "yes" : "no")
            << " | " << group.sourceFormat
            << " | " << group.sourcePaletteFormat
            << " | " << hexByte(group.rawFlags)
            << " | " << hexByte(group.rawDataFormat)
            << " | " << (group.hasMipmaps ? "yes" : "no")
            << " | " << (group.hasInternalPalette ? "yes" : "no")
            << " | " << group.textureCount
            << " | " << group.decodedCount
            << " | " << group.sourceFiles.size()
            << " |\n";
    }
    out << "\n";

    if (!inventory.failures.empty()) {
        out << "## Failures\n\n";
        for (const auto& failure : inventory.failures) {
            out << "- " << failure << "\n";
        }
        out << "\n";
    }

    return out.str();
}

} // namespace spice::mld::analysis
