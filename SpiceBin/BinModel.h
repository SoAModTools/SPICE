#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace spice::bin {

enum class DiagnosticSeverity {
    Info,
    Warning,
    Error,
};

struct BinDiagnostic {
    DiagnosticSeverity severity{ DiagnosticSeverity::Info };
    std::string message{};
    std::uint32_t offset{ 0U };
};

struct BinIndexedRecordSample {
    std::uint32_t sampleIndex{ 0U };
    std::uint32_t tableOffset{ 0U };
    std::uint32_t recordOffset{ 0U };
    bool recordInBounds{ false };
    std::uint32_t word0{ 0U };
    bool word0EqualsDataBaseOffset{ false };
    std::uint32_t word4{ 0U };
    bool word4TargetInBounds{ false };
    std::uint32_t word8{ 0U };
    std::uint32_t word12{ 0U };
    std::uint32_t word16{ 0U };
    std::uint32_t word20{ 0U };
    std::uint32_t word24{ 0U };
    std::string bytes16Hex{};
    std::string bytes32Hex{};
};

struct BinIndexedTableProbe {
    bool present{ false };
    bool headerInBounds{ false };
    std::uint32_t count{ 0U };
    std::uint32_t offsetTableOffset{ 0x04U };
    std::uint32_t offsetTableEndOffset{ 0U };
    std::uint32_t dataBaseOffset{ 0U };
    bool offsetTableInBounds{ false };
    bool offsetsInBounds{ false };
    bool offsetsMonotonic{ false };
    std::uint32_t firstRecordOffset{ 0U };
    std::uint32_t lastRecordOffset{ 0U };
    std::uint32_t sampledRecordCount{ 0U };
    std::string offsetsPreview{};
    std::vector<BinIndexedRecordSample> samples{};
};

struct BinFile {
    std::string sourcePath{};
    std::uint32_t rawSize{ 0U };
    std::vector<std::uint8_t> bytes{};
    BinIndexedTableProbe indexedTableProbe{};
    std::vector<BinDiagnostic> diagnostics{};

    [[nodiscard]] bool ok() const;
};

[[nodiscard]] const char* toString(DiagnosticSeverity severity);

} // namespace spice::bin
