#pragma once

#include "File/ModelFile.h"

#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <vector>

namespace spice::modeling {

enum class ModelingDiagnosticSeverity { Info, Warning, Error };

struct ModelingDiagnostic {
    ModelingDiagnosticSeverity severity{ ModelingDiagnosticSeverity::Info };
    std::string message{};
};

class ModelDocument {
public:
    ModelDocument(const ModelDocument&) = default;
    ModelDocument(ModelDocument&&) noexcept = default;
    ModelDocument& operator=(const ModelDocument&) = default;
    ModelDocument& operator=(ModelDocument&&) noexcept = default;

    [[nodiscard]] ObjectData::Enums::ModelFormat format() const noexcept;
    [[nodiscard]] std::shared_ptr<const ObjectData::Node> root() const noexcept;

private:
    friend class ModelDocumentCodec;
    ModelDocument(File::ModelFile model, std::vector<std::uint8_t> encodedBytes);

    File::ModelFile model_{};
    std::vector<std::uint8_t> encodedBytes_{};
};

struct ModelDocumentDecodeResult {
    std::shared_ptr<const ModelDocument> document{};
    std::vector<ModelingDiagnostic> diagnostics{};
    [[nodiscard]] bool ok() const noexcept { return document != nullptr; }
};

struct ModelDocumentEncodeResult {
    std::vector<std::uint8_t> bytes{};
    std::vector<ModelingDiagnostic> diagnostics{};
    [[nodiscard]] bool ok() const noexcept;
};

class ModelDocumentCodec {
public:
    [[nodiscard]] static ModelDocumentDecodeResult decode(
        std::span<const std::uint8_t> bytes,
        std::uint32_t address = 0U);
    [[nodiscard]] static ModelDocumentEncodeResult encode(const ModelDocument& document);
};

} // namespace spice::modeling
