#include "StdDocumentWriter.h"

#include "../Compression/Aklz.h"
#include "../SpiceRoot/Binary/EndianWriter.h"

#include <algorithm>
#include <map>
#include <type_traits>

namespace spice::stdfile {
namespace {

using spice::root::EndianWriter;

std::vector<std::uint8_t> encodeActionView(const StdActionViewPayload& payload, const spice::root::Endian endian) {
    EndianWriter writer(endian);
    writer.reserve(kStdActionViewPayloadSize);
    writer.write_i16(payload.primaryActionKey);
    writer.write_i16(payload.routeSecondaryKey);
    writer.write_i16(payload.directSecondaryKey);
    writer.write_u16(payload.rawLowFlags);
    writer.write_u32(payload.raw08);
    writer.write_u32(payload.raw0c);
    writer.write_u32(payload.actionViewFlags);
    writer.write_u32(payload.modeLocalValueBits);
    writer.write_i16(payload.startFrame);
    writer.write_u16(payload.raw1a);
    writer.write_i16(payload.endFrame);
    writer.write_i16(payload.holdFrameCount);
    writer.write_i16(payload.stepFrameCount);
    writer.write_i16(payload.requestedMode);
    return writer.take_data();
}

std::vector<std::uint8_t> encodePayload(const StdEntryPayload& payload, const spice::root::Endian endian) {
    if (const auto* typed = std::get_if<StdActionViewPayload>(&payload.content)) return encodeActionView(*typed, endian);
    return std::get<StdOpaquePayload>(payload.content).bytes;
}

std::vector<std::uint8_t> serializeDecoded(const StdDocument& document, const spice::root::Endian endian) {
    if (const auto* opaque = std::get_if<StdOpaqueContent>(&document.content)) return opaque->decodedBytes;

    EndianWriter writer(endian);
    if (const auto* rows = std::get_if<StdActionRowsContent>(&document.content)) {
        writer.reserve(0x10U + rows->rows.size() * 0x18U);
        writer.write_u16(rows->rawCommandLow);
        writer.write_u16(rows->rawCommandHigh);
        writer.write_u32(rows->rawLoaderContextWord);
        writer.write_u32(static_cast<std::uint32_t>(rows->rows.size()));
        writer.write_u32(rows->rawRowTablePointerWord);
        for (const auto& row : rows->rows) {
            writer.write_i16(row.actionId);
            writer.write_i16(row.rowType);
            writer.write_i16(row.callbackIndex);
            writer.write_i16(row.raw06);
            writer.write_u32(row.flags);
            writer.write_i16(row.secondaryKey);
            writer.write_i16(row.raw0e);
            writer.write_u32(row.raw10Bits);
            writer.write_u32(row.raw14Bits);
        }
        return writer.take_data();
    }

    const auto& table = std::get<StdEntryTableContent>(document.content);
    const auto recordCount = table.records.size() + 1U;
    writer.resize(0x10U + recordCount * 0x10U, 0U);
    writer.write_u16_at(0x00U, static_cast<std::uint16_t>(recordCount));
    writer.write_u16_at(0x02U, table.kind);
    writer.write_u32_at(0x04U, table.rawHeader04);
    writer.write_u32_at(0x08U, table.rawHeader08);

    struct PayloadLocation { std::uint32_t relativeOffset{ 0U }; std::uint32_t size{ 0U }; };
    std::map<std::uint64_t, PayloadLocation> locations{};
    for (const auto& item : table.payloadLayout) {
        std::visit([&](const auto id) {
            using Id = std::remove_cv_t<decltype(id)>;
            if constexpr (std::is_same_v<Id, StdEntryPayloadId>) {
                const auto* payload = findEntryPayload(table, id);
                const auto bytes = encodePayload(*payload, endian);
                locations[id.value] = PayloadLocation{
                    static_cast<std::uint32_t>(writer.data().size() - 0x10U),
                    static_cast<std::uint32_t>(bytes.size()) };
                writer.write_bytes(bytes);
            } else {
                writer.write_bytes(findOpaqueFragment(table, id)->bytes);
            }
        }, item);
    }

    writer.write_u32_at(0x0cU, static_cast<std::uint32_t>(writer.data().size() - 0x10U));
    for (std::size_t index = 0U; index < table.records.size(); ++index) {
        const auto& record = table.records[index];
        const auto offset = 0x10U + index * 0x10U;
        writer.write_i16_at(offset + 0x00U, record.locationCode);
        writer.write_i16_at(offset + 0x02U, record.opcode);
        writer.write_u32_at(offset + 0x04U, record.raw04);
        if (record.payload.has_value()) {
            const auto location = locations.at(record.payload->value);
            writer.write_u32_at(offset + 0x08U, location.size);
            writer.write_u32_at(offset + 0x0cU, location.relativeOffset);
        }
    }
    const auto terminatorOffset = 0x10U + table.records.size() * 0x10U;
    writer.write_i16_at(terminatorOffset + 0x00U, table.terminator.negativeLocation);
    writer.write_i16_at(terminatorOffset + 0x02U, table.terminator.raw02);
    writer.write_u32_at(terminatorOffset + 0x04U, table.terminator.raw04);
    writer.write_u32_at(terminatorOffset + 0x08U, table.terminator.raw08);
    writer.write_u32_at(terminatorOffset + 0x0cU, table.terminator.raw0c);
    return writer.take_data();
}

} // namespace

bool StdDocumentWriteResult::ok() const noexcept {
    return !bytes.empty() && std::none_of(diagnostics.begin(), diagnostics.end(), [](const auto& item) {
        return item.severity == StdDiagnosticSeverity::Error;
    });
}

StdDocumentWriteResult StdDocumentWriter::write(
    const StdDocument& document, const StdWriteTarget& target, const StdImportReceipt* receipt) {
    StdDocumentWriteResult result{};
    auto validation = StdDocumentValidator::validate(document, target, receipt);
    result.diagnostics = std::move(validation.diagnostics);
    if (!validation.ok()) return result;

    auto decoded = serializeDecoded(document, byteOrderFor(target.platform));
    if (target.compression == StdCompression::None) {
        result.bytes = std::move(decoded);
        return result;
    }
    auto encoded = spice::compression::aklz::compress(decoded);
    if (!encoded.ok()) {
        result.diagnostics.push_back(StdDocumentDiagnostic{
            StdDiagnosticCode::AklzEncodeFailed, StdDiagnosticSeverity::Error,
            "AKLZ compression failed: " + std::string(spice::compression::aklz::errorToString(encoded.error)), std::nullopt });
        return result;
    }
    result.bytes = std::move(encoded.bytes);
    return result;
}

} // namespace spice::stdfile
