#include "MotionDocument.h"

#include "File/AnimationFile.h"

#include <exception>

namespace spice::modeling {
namespace {

[[nodiscard]] std::span<const std::byte> asBytes(const std::span<const std::uint8_t> bytes) {
    return { reinterpret_cast<const std::byte*>(bytes.data()), bytes.size() };
}

} // namespace

MotionDocument::MotionDocument(Animation::Motion motion, Animation::MotionTargetLayout targetLayout,
    const Animation::EulerRecordWidth eulerWidth, const MotionKind kind,
    std::vector<std::uint8_t> encodedBytes)
    : motion_(std::move(motion)), targetLayout_(std::move(targetLayout)), eulerWidth_(eulerWidth),
      kind_(kind), encodedBytes_(std::move(encodedBytes)) {}

MotionDocumentDecodeResult MotionDocumentCodec::decode(
    const std::span<const std::uint8_t> bytes,
    const MotionDecodeContext& context) {
    MotionDocumentDecodeResult result{};
    if (bytes.empty()) {
        result.diagnostics.push_back({ ModelingDiagnosticSeverity::Error, "Motion input is empty." });
        return result;
    }
    try {
        const auto byteView = asBytes(bytes);
        auto structure = File::AnimationFile::parse_structure(byteView, context.address);
        if (structure.status == File::NinjaMotionParseStatus::Failed) {
            result.diagnostics.push_back({ ModelingDiagnosticSeverity::Error,
                "The input does not contain a supported Ninja motion block." });
            return result;
        }
        auto decoded = File::AnimationFile::read_from_bytes(
            byteView, context.targetLayout, context.eulerWidth, context.address);
        MotionKind kind = MotionKind::Unknown;
        if (structure.header.kind == File::NinjaMotionKind::Node) kind = MotionKind::Node;
        else if (structure.header.kind == File::NinjaMotionKind::Shape) kind = MotionKind::Shape;
        else if (structure.header.kind == File::NinjaMotionKind::Camera) kind = MotionKind::Camera;
        result.document = std::shared_ptr<const MotionDocument>(new MotionDocument(
            std::move(decoded.animation), context.targetLayout, context.eulerWidth,
            kind, { bytes.begin(), bytes.end() }));
    } catch (const std::exception& error) {
        result.diagnostics.push_back({ ModelingDiagnosticSeverity::Error, error.what() });
    }
    return result;
}

MotionDocumentEncodeResult MotionDocumentCodec::encode(const MotionDocument& document) {
    MotionDocumentEncodeResult result{};
    result.bytes = document.encodedBytes_;
    if (result.bytes.empty()) {
        result.diagnostics.push_back({ ModelingDiagnosticSeverity::Error,
            "This motion document has no decoder-owned encoding to emit." });
    }
    return result;
}

} // namespace spice::modeling
