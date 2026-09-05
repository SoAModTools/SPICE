#pragma once

#include "Animation/Motion.h"
#include "Animation/MotionTargetLayout.h"
#include "ModelDocument.h"

#include <cstdint>
#include <memory>
#include <span>
#include <vector>

namespace spice::modeling {

enum class MotionKind { Node, Shape, Camera, Unknown };

struct MotionDecodeContext {
    Animation::MotionTargetLayout targetLayout{};
    Animation::EulerRecordWidth eulerWidth{ Animation::EulerRecordWidth::Full32 };
    std::uint32_t address{ 0U };
};

class MotionDocument {
public:
    MotionDocument(const MotionDocument&) = default;
    MotionDocument(MotionDocument&&) noexcept = default;
    MotionDocument& operator=(const MotionDocument&) = default;
    MotionDocument& operator=(MotionDocument&&) noexcept = default;

    [[nodiscard]] const Animation::Motion& motion() const noexcept { return motion_; }
    [[nodiscard]] const Animation::MotionTargetLayout& targetLayout() const noexcept { return targetLayout_; }
    [[nodiscard]] Animation::EulerRecordWidth eulerWidth() const noexcept { return eulerWidth_; }
    [[nodiscard]] MotionKind kind() const noexcept { return kind_; }

private:
    friend class MotionDocumentCodec;
    MotionDocument(Animation::Motion motion, Animation::MotionTargetLayout targetLayout,
        Animation::EulerRecordWidth eulerWidth, MotionKind kind,
        std::vector<std::uint8_t> encodedBytes);

    Animation::Motion motion_{};
    Animation::MotionTargetLayout targetLayout_{};
    Animation::EulerRecordWidth eulerWidth_{ Animation::EulerRecordWidth::Full32 };
    MotionKind kind_{ MotionKind::Unknown };
    std::vector<std::uint8_t> encodedBytes_{};
};

struct MotionDocumentDecodeResult {
    std::shared_ptr<const MotionDocument> document{};
    std::vector<ModelingDiagnostic> diagnostics{};
    [[nodiscard]] bool ok() const noexcept { return document != nullptr; }
};

using MotionDocumentEncodeResult = ModelDocumentEncodeResult;

class MotionDocumentCodec {
public:
    [[nodiscard]] static MotionDocumentDecodeResult decode(
        std::span<const std::uint8_t> bytes,
        const MotionDecodeContext& context);
    [[nodiscard]] static MotionDocumentEncodeResult encode(const MotionDocument& document);
};

} // namespace spice::modeling
