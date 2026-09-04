#pragma once

#include "SstSmlDocument.h"
#include "../SpiceRoot/Binary/Endian.h"

#include <array>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace spice::sstsml {

enum class SstSmlDiagnosticSeverity { Info, Warning, Error };
enum class SstSmlSourceMember { Pair, Sml, Sst };
enum class SstSmlSourceWrapper { Raw, Aklz };

struct SstSmlDocumentDiagnostic {
    SstSmlDiagnosticSeverity severity{ SstSmlDiagnosticSeverity::Info };
    SstSmlSourceMember source{ SstSmlSourceMember::Pair };
    std::string message{};
    std::optional<std::uint64_t> decodedOffset{};
};

struct SstSmlSourceReceipt {
    std::optional<std::filesystem::path> path{};
    bool bytesRead{ false };
    std::array<std::uint8_t, 32U> rawSha256{};
    std::uint64_t rawSize{ 0U };
    std::optional<std::uint64_t> decodedSize{};
    std::optional<SstSmlSourceWrapper> wrapper{};
    std::optional<spice::root::Endian> endian{};
};

struct SstSmlDocumentImportReceipt {
    SstSmlSourceReceipt sml{};
    SstSmlSourceReceipt sst{};
};

struct SstSmlDocumentImportResult {
    std::optional<SstSmlDocument> document{};
    SstSmlDocumentImportReceipt receipt{};
    std::vector<SstSmlDocumentDiagnostic> diagnostics{};
    [[nodiscard]] bool ok() const;
};

class SstSmlDocumentImporter {
public:
    [[nodiscard]] static SstSmlDocumentImportResult importBytes(
        std::span<const std::uint8_t> smlBytes,
        std::span<const std::uint8_t> sstBytes);

    [[nodiscard]] static SstSmlDocumentImportResult importFile(
        const std::filesystem::path& eitherPairPath);
};

[[nodiscard]] const char* toString(SstSmlDiagnosticSeverity severity) noexcept;
[[nodiscard]] const char* toString(SstSmlSourceMember source) noexcept;
[[nodiscard]] const char* toString(SstSmlSourceWrapper wrapper) noexcept;

} // namespace spice::sstsml
