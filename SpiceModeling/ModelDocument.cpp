#include "ModelDocument.h"

#include <algorithm>
#include <exception>

namespace spice::modeling {
namespace {

[[nodiscard]] std::span<const std::byte> asBytes(const std::span<const std::uint8_t> bytes) {
    return { reinterpret_cast<const std::byte*>(bytes.data()), bytes.size() };
}

} // namespace

ModelDocument::ModelDocument(File::ModelFile model, std::vector<std::uint8_t> encodedBytes)
    : model_(std::move(model)), encodedBytes_(std::move(encodedBytes)) {}

ObjectData::Enums::ModelFormat ModelDocument::format() const noexcept { return model_.format; }

std::shared_ptr<const ObjectData::Node> ModelDocument::root() const noexcept { return model_.model; }

bool ModelDocumentEncodeResult::ok() const noexcept {
    return !bytes.empty() && std::none_of(diagnostics.begin(), diagnostics.end(), [](const auto& item) {
        return item.severity == ModelingDiagnosticSeverity::Error;
    });
}

ModelDocumentDecodeResult ModelDocumentCodec::decode(
    const std::span<const std::uint8_t> bytes,
    const std::uint32_t address) {
    ModelDocumentDecodeResult result{};
    if (bytes.empty()) {
        result.diagnostics.push_back({ ModelingDiagnosticSeverity::Error, "Model input is empty." });
        return result;
    }
    try {
        auto model = File::ModelFile::read_from_bytes(asBytes(bytes), address);
        result.document = std::shared_ptr<const ModelDocument>(
            new ModelDocument(std::move(model), { bytes.begin(), bytes.end() }));
    } catch (const std::exception& error) {
        result.diagnostics.push_back({ ModelingDiagnosticSeverity::Error, error.what() });
    }
    return result;
}

ModelDocumentEncodeResult ModelDocumentCodec::encode(const ModelDocument& document) {
    ModelDocumentEncodeResult result{};
    result.bytes = document.encodedBytes_;
    if (result.bytes.empty()) {
        result.diagnostics.push_back({ ModelingDiagnosticSeverity::Error,
            "This model document has no decoder-owned encoding to emit." });
    }
    return result;
}

} // namespace spice::modeling
