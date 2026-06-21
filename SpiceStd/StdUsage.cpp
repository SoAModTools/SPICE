#include "StdUsage.h"

#include "../Compression/Aklz.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <fstream>
#include <iomanip>
#include <iterator>
#include <limits>
#include <map>
#include <sstream>
#include <span>
#include <stdexcept>
#include <string_view>

namespace spice::stdfile {
namespace {

std::string toLowerCopy(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return value;
}

bool isStdPath(const std::filesystem::path& path) {
    return toLowerCopy(path.extension().string()) == ".std";
}

bool allDigits(std::string_view value) {
    return !value.empty() && std::all_of(value.begin(), value.end(), [](unsigned char c) {
        return std::isdigit(c) != 0;
    });
}

std::vector<std::filesystem::path> collectStdPaths(const std::filesystem::path& inputPath, bool& inputWasDirectory) {
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
        if (isStdPath(inputPath)) {
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
        if (isStdPath(entry.path())) {
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

std::string directoryString(const std::string& relativePath) {
    const auto slash = relativePath.find_last_of('/');
    if (slash == std::string::npos) {
        return {};
    }
    return relativePath.substr(0U, slash);
}

std::string firstPathComponent(const std::string& relativePath) {
    const auto slash = relativePath.find('/');
    if (slash == std::string::npos) {
        return {};
    }
    return relativePath.substr(0U, slash);
}

std::string pathStem(const std::filesystem::path& path) {
    return path.stem().generic_string();
}

std::vector<std::uint8_t> readAllBytes(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        std::ostringstream message;
        message << "Could not open input file: " << path.string();
        throw std::runtime_error(message.str());
    }

    return std::vector<std::uint8_t>(
        std::istreambuf_iterator<char>(in),
        std::istreambuf_iterator<char>());
}

std::uint32_t sizeToU32Saturated(std::size_t size) {
    if (size > std::numeric_limits<std::uint32_t>::max()) {
        return std::numeric_limits<std::uint32_t>::max();
    }
    return static_cast<std::uint32_t>(size);
}

std::string hexPrefix(std::span<const std::uint8_t> bytes, std::size_t maxBytes) {
    std::ostringstream out;
    out << std::hex << std::setfill('0');
    const auto count = std::min(bytes.size(), maxBytes);
    for (std::size_t i = 0U; i < count; ++i) {
        out << std::setw(2) << static_cast<unsigned>(bytes[i]);
    }
    return out.str();
}

std::vector<std::string> printableStrings(std::span<const std::uint8_t> bytes) {
    constexpr std::size_t kMinLength = 4U;
    constexpr std::size_t kMaxStrings = 8U;
    constexpr std::size_t kMaxStringLength = 64U;

    std::vector<std::string> result{};
    std::string current{};
    const auto flush = [&]() {
        if (current.size() >= kMinLength) {
            result.push_back(current);
        }
        current.clear();
    };

    for (const auto byte : bytes) {
        if (byte >= 0x20U && byte <= 0x7eU) {
            if (current.size() < kMaxStringLength) {
                current.push_back(static_cast<char>(byte));
            }
        } else {
            flush();
            if (result.size() >= kMaxStrings) {
                return result;
            }
        }
    }
    flush();
    if (result.size() > kMaxStrings) {
        result.resize(kMaxStrings);
    }
    return result;
}

StdUsageBucket classifyUsageBucket(const std::string& relativePath, const std::string& stem) {
    const auto firstComponent = toLowerCopy(firstPathComponent(relativePath));
    const auto lowerStem = toLowerCopy(stem);
    if (firstComponent != "bchara") {
        return firstComponent.empty() ? StdUsageBucket::Unknown : StdUsageBucket::OtherDirectory;
    }

    if (lowerStem == "common") {
        return StdUsageBucket::BcharaCommon;
    }
    if (lowerStem == "damage") {
        return StdUsageBucket::BcharaDamage;
    }
    if (lowerStem.size() >= 3U && lowerStem.rfind("cr", 0U) == 0U && allDigits(std::string_view(lowerStem).substr(2U))) {
        return StdUsageBucket::BcharaCharacterResource;
    }
    if (!lowerStem.empty() && lowerStem[0] == 'm') {
        return StdUsageBucket::BcharaMFamily;
    }
    return StdUsageBucket::BcharaOther;
}

bool isBcharaBucket(StdUsageBucket bucket) {
    return bucket == StdUsageBucket::BcharaMFamily ||
        bucket == StdUsageBucket::BcharaCommon ||
        bucket == StdUsageBucket::BcharaDamage ||
        bucket == StdUsageBucket::BcharaCharacterResource ||
        bucket == StdUsageBucket::BcharaOther;
}

std::string boolText(bool value) {
    return value ? "true" : "false";
}

std::string csvEscape(const std::string& value) {
    const bool needsQuotes = value.find_first_of(",\"\r\n") != std::string::npos;
    if (!needsQuotes) {
        return value;
    }

    std::string escaped = "\"";
    for (const char c : value) {
        if (c == '"') {
            escaped += "\"\"";
        } else {
            escaped += c;
        }
    }
    escaped += '"';
    return escaped;
}

std::string joinStrings(const std::vector<std::string>& values) {
    std::ostringstream out;
    for (std::size_t i = 0U; i < values.size(); ++i) {
        if (i != 0U) {
            out << " | ";
        }
        out << values[i];
    }
    return out.str();
}

void writeTextFile(const std::filesystem::path& path, const std::string& text) {
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

} // namespace

const char* toString(StdUsageBucket bucket) {
    switch (bucket) {
    case StdUsageBucket::Unknown:
        return "unknown";
    case StdUsageBucket::BcharaMFamily:
        return "bchara_m_family";
    case StdUsageBucket::BcharaCommon:
        return "bchara_common";
    case StdUsageBucket::BcharaDamage:
        return "bchara_damage";
    case StdUsageBucket::BcharaCharacterResource:
        return "bchara_character_resource";
    case StdUsageBucket::BcharaOther:
        return "bchara_other";
    case StdUsageBucket::OtherDirectory:
        return "other_directory";
    }
    return "unknown";
}

StdUsageScanResult scanStdUsage(const std::filesystem::path& inputPath) {
    StdUsageScanResult result{};
    result.inputPath = inputPath.string();

    const auto paths = collectStdPaths(inputPath, result.inputWasDirectory);
    result.files.reserve(paths.size());

    for (const auto& path : paths) {
        StdUsageFile file{};
        file.relativePath = relativePathString(path, inputPath, result.inputWasDirectory);
        file.absolutePath = std::filesystem::absolute(path).string();
        file.directory = directoryString(file.relativePath);
        file.stem = pathStem(path);
        file.usageBucket = classifyUsageBucket(file.relativePath, file.stem);
        file.alxKnownCoveredPattern = file.usageBucket == StdUsageBucket::BcharaMFamily;

        auto rawBytes = readAllBytes(path);
        file.rawSize = sizeToU32Saturated(rawBytes.size());

        std::vector<std::uint8_t> decodedBytes{};
        if (spice::compression::aklz::isAklz(rawBytes)) {
            file.sourceWasCompressedAklz = true;
            const auto decoded = spice::compression::aklz::decompress(rawBytes);
            if (!decoded.ok()) {
                file.decodedOk = false;
                file.decodeError = std::string(spice::compression::aklz::errorToString(decoded.error));
                file.decodedSize = 0U;
                result.files.push_back(std::move(file));
                continue;
            }
            decodedBytes = decoded.bytes;
        } else {
            decodedBytes = std::move(rawBytes);
        }

        file.decodedSize = sizeToU32Saturated(decodedBytes.size());
        file.decodedHeader16Hex = hexPrefix(decodedBytes, 16U);
        file.decodedHeader32Hex = hexPrefix(decodedBytes, 32U);
        file.printableStrings = printableStrings(decodedBytes);
        result.files.push_back(std::move(file));
    }

    return result;
}

StdUsageSummary summarizeStdUsage(const StdUsageScanResult& scan) {
    StdUsageSummary summary{};
    summary.fileCount = scan.files.size();
    for (const auto& file : scan.files) {
        if (file.sourceWasCompressedAklz) {
            ++summary.aklzCompressedFileCount;
        }
        if (!file.decodedOk) {
            ++summary.decodeErrorCount;
        }
        if (file.alxKnownCoveredPattern) {
            ++summary.alxKnownCoveredPatternCount;
        }
        if (isBcharaBucket(file.usageBucket)) {
            ++summary.bcharaFileCount;
        } else if (file.usageBucket == StdUsageBucket::OtherDirectory) {
            ++summary.otherDirectoryFileCount;
        }
    }
    return summary;
}

std::vector<StdUsageBucketSummary> summarizeStdUsageBuckets(const StdUsageScanResult& scan) {
    std::map<StdUsageBucket, StdUsageBucketSummary> byBucket{};
    for (const auto& file : scan.files) {
        auto& bucket = byBucket[file.usageBucket];
        bucket.bucket = file.usageBucket;
        ++bucket.fileCount;
        if (file.sourceWasCompressedAklz) {
            ++bucket.aklzCompressedFileCount;
        }
        if (!file.decodedOk) {
            ++bucket.decodeErrorCount;
        }
        if (file.alxKnownCoveredPattern) {
            ++bucket.alxKnownCoveredPatternCount;
        }
    }

    std::vector<StdUsageBucketSummary> result{};
    result.reserve(byBucket.size());
    for (const auto& [unused, bucket] : byBucket) {
        (void)unused;
        result.push_back(bucket);
    }
    return result;
}

std::string formatStdUsageFilesCsv(const StdUsageScanResult& scan) {
    std::ostringstream out;
    out << "path,absolutePath,directory,stem,usageBucket,alxKnownCoveredPattern,"
           "rawSize,decodedSize,sourceWasCompressedAklz,decodedOk,decodeError,"
           "decodedHeader16Hex,decodedHeader32Hex,printableStrings\n";
    for (const auto& file : scan.files) {
        out << csvEscape(file.relativePath) << ","
            << csvEscape(file.absolutePath) << ","
            << csvEscape(file.directory) << ","
            << csvEscape(file.stem) << ","
            << toString(file.usageBucket) << ","
            << boolText(file.alxKnownCoveredPattern) << ","
            << file.rawSize << ","
            << file.decodedSize << ","
            << boolText(file.sourceWasCompressedAklz) << ","
            << boolText(file.decodedOk) << ","
            << csvEscape(file.decodeError) << ","
            << csvEscape(file.decodedHeader16Hex) << ","
            << csvEscape(file.decodedHeader32Hex) << ","
            << csvEscape(joinStrings(file.printableStrings)) << "\n";
    }
    return out.str();
}

std::string formatStdUsageBucketsCsv(const StdUsageScanResult& scan) {
    std::ostringstream out;
    out << "usageBucket,fileCount,aklzCompressedFileCount,decodeErrorCount,alxKnownCoveredPatternCount\n";
    for (const auto& bucket : summarizeStdUsageBuckets(scan)) {
        out << toString(bucket.bucket) << ","
            << bucket.fileCount << ","
            << bucket.aklzCompressedFileCount << ","
            << bucket.decodeErrorCount << ","
            << bucket.alxKnownCoveredPatternCount << "\n";
    }
    return out.str();
}

StdUsageWriteResult writeStdUsageArtifacts(
    const StdUsageScanResult& scan,
    const std::filesystem::path& outputDir) {
    std::filesystem::create_directories(outputDir);

    StdUsageWriteResult result{};
    result.filesCsvPath = outputDir / "std_usage_files.csv";
    result.bucketsCsvPath = outputDir / "std_usage_buckets.csv";

    writeTextFile(result.filesCsvPath, formatStdUsageFilesCsv(scan));
    writeTextFile(result.bucketsCsvPath, formatStdUsageBucketsCsv(scan));
    return result;
}

} // namespace spice::stdfile
