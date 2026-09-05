#pragma once

#include "MldDocument.h"
#include "Model/MldDiagnostics.h"
#include "../SpiceRoot/Binary/Endian.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace spice::mld {

enum class MldPlatform { Dreamcast, GameCube };
enum class MldWrapper { Raw, Aklz };
enum class MldDiagnosticSeverity { Info, Warning, Error };

struct MldDocumentDiagnostic {
    MldDiagnosticSeverity severity{ MldDiagnosticSeverity::Info };
    std::string message{};
    std::optional<std::uint64_t> decodedOffset{};
};

struct MldReceiptLayoutItem {
    MldLayoutItem item{};
    std::uint64_t decodedOffset{ 0U };
    std::uint64_t encodedSize{ 0U };
    std::uint64_t encodedReference{ 0U };
};

namespace detail { struct MldImportState; }

class MldImportReceipt {
public:
    MldImportReceipt() = default;

    std::optional<std::filesystem::path> path{};
    MldPlatform platform{ MldPlatform::GameCube };
    MldWrapper wrapper{ MldWrapper::Raw };
    spice::root::Endian endian{ spice::root::Endian::Big };
    std::array<std::uint8_t, 32U> sourceSha256{};
    std::uint64_t sourceSize{ 0U };
    std::uint64_t decodedSize{ 0U };
    std::vector<MldReceiptLayoutItem> layout{};

private:
    std::shared_ptr<const detail::MldImportState> state_{};
    friend class MldDocumentImporter;
    friend class MldDocumentWriter;
    friend class MldDocumentValidator;
};

struct MldDocumentImportResult {
    std::optional<MldDocument> document{};
    MldImportReceipt receipt{};
    std::vector<MldDocumentDiagnostic> diagnostics{};
    [[nodiscard]] bool ok() const noexcept;
};

struct MldImportOptions {
    std::optional<MldPlatform> platformHint{};
};

class MldDocumentImporter {
public:
    [[nodiscard]] static MldDocumentImportResult importBytes(
        std::span<const std::uint8_t> bytes,
        const MldImportOptions& options = {});
    [[nodiscard]] static MldDocumentImportResult importFile(
        const std::filesystem::path& path,
        const MldImportOptions& options = {});
};

} // namespace spice::mld
