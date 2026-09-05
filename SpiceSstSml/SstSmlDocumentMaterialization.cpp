#include "SstSmlDocumentMaterialization.h"

#include "SstParser.h"
#include "../SpiceRoot/Binary/EndianWriter.h"

#include <algorithm>
#include <type_traits>

namespace spice::sstsml {
namespace {

void addError(SstSmlMaterializationResult& result, std::string message) {
    result.diagnostics.push_back({ SstSmlDiagnosticSeverity::Error,
        SstSmlSourceMember::Pair, std::move(message) });
}

void writeField(spice::root::EndianSpanWriter& writer, const SstCommandField& field) {
    std::visit([&](const auto value) {
        using Value = std::decay_t<decltype(value)>;
        if constexpr (std::is_same_v<Value, std::int8_t>) writer.write_i8_at(field.payloadOffset, value);
        else if constexpr (std::is_same_v<Value, std::uint8_t>) writer.write_u8_at(field.payloadOffset, value);
        else if constexpr (std::is_same_v<Value, std::int16_t>) writer.write_i16_at(field.payloadOffset, value);
        else if constexpr (std::is_same_v<Value, std::uint16_t>) writer.write_u16_at(field.payloadOffset, value);
        else if constexpr (std::is_same_v<Value, std::uint32_t>) writer.write_u32_at(field.payloadOffset, value);
        else if constexpr (std::is_same_v<Value, float>) writer.write_f32_at(field.payloadOffset, value);
    }, field.value);
}

void writeLightingRow(spice::root::EndianSpanWriter& writer,
    const SstLightingRow& row,
    const std::uint32_t base) {
    writer.write_i8_at(base + 0x00U, row.state);
    writer.write_i16_at(base + 0x02U, row.classSelector);
    writer.write_u32_at(base + 0x04U, row.flags);
    writer.write_i16_at(base + 0x08U, row.runtimeSlotId);
    for (std::uint32_t index = 0U; index < 3U; ++index) {
        writer.write_f32_at(base + 0x0CU + index * 4U, row.lightVector[index]);
        writer.write_f32_at(base + 0x30U + index * 4U, row.slotRgb[index]);
        writer.write_f32_at(base + 0x3CU + index * 4U, row.globalRgb[index]);
    }
    writer.write_f32_at(base + 0x48U, row.attenuationOrSpot0);
    writer.write_f32_at(base + 0x4CU, row.attenuationOrSpot1);
    writer.write_u32_at(base + 0x64U, row.rawTailWord);
}

} // namespace

bool SstSmlMaterializationResult::ok() const noexcept {
    return std::none_of(diagnostics.begin(), diagnostics.end(), [](const auto& diagnostic) {
        return diagnostic.severity == SstSmlDiagnosticSeverity::Error;
    });
}

SstSmlMaterializationResult materializeCommandPayload(
    const SstStageCommand& command,
    const spice::root::Endian endian) {
    SstSmlMaterializationResult result{};
    if (!command.payloadSpanKnown) return result;
    const auto payloadSize = detail::SstParser::commandPayloadSize(command.type);
    result.bytes.resize(payloadSize, 0U);
    for (const auto& fragment : command.opaquePayloadFragments) {
        if (fragment.payloadOffset > result.bytes.size() ||
            fragment.bytes.size() > result.bytes.size() - fragment.payloadOffset) {
            addError(result, "SST command opaque fragment is outside its payload");
            return result;
        }
        std::copy(fragment.bytes.begin(), fragment.bytes.end(),
            result.bytes.begin() + static_cast<std::ptrdiff_t>(fragment.payloadOffset));
    }
    spice::root::EndianSpanWriter writer(result.bytes, endian);
    for (const auto& field : command.fields) writeField(writer, field);
    if (command.placement) {
        const auto& placement = *command.placement;
        writer.write_f32_at(0x1CU, placement.positionX);
        writer.write_f32_at(0x20U, placement.positionY);
        writer.write_f32_at(0x24U, placement.positionZ);
        writer.write_u32_at(0x28U, placement.rotationAngleX);
        writer.write_u32_at(0x2CU, placement.rotationAngleY);
        writer.write_u32_at(0x30U, placement.rotationAngleZ);
        writer.write_f32_at(0x34U, placement.scaleX);
        writer.write_f32_at(0x38U, placement.scaleY);
        writer.write_f32_at(0x3CU, placement.scaleZ);
    }
    for (std::size_t index = 0U; index < command.lightingRows.size(); ++index) {
        writeLightingRow(writer, command.lightingRows[index], static_cast<std::uint32_t>(index * 0x68U));
    }
    return result;
}

SstSmlMaterializationResult materializeEmbeddedResource(
    const SmlEmbeddedResource& resource,
    const SstSmlDocumentImportReceipt& receipt,
    const std::optional<spice::mld::MldWriteTarget> fallbackTarget) {
    SstSmlMaterializationResult result{};
    if (const auto* opaque = std::get_if<SmlOpaqueEmbeddedResource>(&resource.content)) {
        result.bytes = opaque->bytes;
        return result;
    }
    const auto& document = std::get<spice::mld::MldDocument>(resource.content);
    const auto* nestedReceipt = receipt.embeddedMld(resource.id);
    std::optional<spice::mld::MldWriteTarget> target = fallbackTarget;
    if (nestedReceipt) target = spice::mld::MldWriteTarget{ nestedReceipt->platform, nestedReceipt->wrapper };
    if (!target) {
        addError(result, "Decoded embedded MLD requires its import receipt or an explicit output target");
        return result;
    }
    const auto written = spice::mld::MldDocumentWriter::write(document, *target, nestedReceipt);
    for (const auto& diagnostic : written.diagnostics) {
        result.diagnostics.push_back({
            diagnostic.severity == spice::mld::MldDiagnosticSeverity::Error
                ? SstSmlDiagnosticSeverity::Error
                : diagnostic.severity == spice::mld::MldDiagnosticSeverity::Warning
                    ? SstSmlDiagnosticSeverity::Warning : SstSmlDiagnosticSeverity::Info,
            SstSmlSourceMember::Sml,
            "Embedded MLD: " + diagnostic.message,
            diagnostic.decodedOffset,
            resource.id,
        });
    }
    if (written.ok()) result.bytes = written.bytes;
    return result;
}

} // namespace spice::sstsml
