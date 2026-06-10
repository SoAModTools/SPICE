#include "MldFileExporter.h"

#include "../../Compression/Aklz.h"
#include "../../SpiceCore/Binary/EndianReader.h"

#include <algorithm>
#include <array>
#include <bit>
#include <cstring>
#include <optional>
#include <span>
#include <stdexcept>
#include <unordered_set>

namespace soasim::mld::exporting {
namespace {

using spice::core::Endian;
using spice::core::EndianReader;

constexpr std::size_t kMldHeaderSize = 0x14U;
constexpr std::size_t kEntrySize = 0x68U;

[[nodiscard]] Endian endianForPlatform(const model::TargetPlatform platform) {
    switch (platform) {
    case model::TargetPlatform::Dreamcast:
        return Endian::Little;
    case model::TargetPlatform::GameCube:
    case model::TargetPlatform::Unknown:
    default:
        return Endian::Big;
    }
}

void writeAscii(std::vector<std::uint8_t>& out, const std::size_t offset, const std::string& value, const std::size_t maxLen) {
    if (offset > out.size() || maxLen > out.size() - offset) {
        return;
    }
    std::fill(out.begin() + static_cast<std::ptrdiff_t>(offset),
        out.begin() + static_cast<std::ptrdiff_t>(offset + maxLen),
        0U);
    const auto count = std::min(maxLen, value.size());
    for (std::size_t i = 0; i < count; ++i) {
        out[offset + i] = static_cast<std::uint8_t>(value[i]);
    }
}

void writeU16(std::vector<std::uint8_t>& out, const std::size_t offset, const std::uint16_t value, const Endian endian) {
    if (offset > out.size() || 2U > out.size() - offset) {
        return;
    }
    if (endian == Endian::Big) {
        out[offset + 0U] = static_cast<std::uint8_t>((value >> 8U) & 0xFFU);
        out[offset + 1U] = static_cast<std::uint8_t>(value & 0xFFU);
    } else {
        out[offset + 0U] = static_cast<std::uint8_t>(value & 0xFFU);
        out[offset + 1U] = static_cast<std::uint8_t>((value >> 8U) & 0xFFU);
    }
}

void writeU32(std::vector<std::uint8_t>& out, const std::size_t offset, const std::uint32_t value, const Endian endian) {
    if (offset > out.size() || 4U > out.size() - offset) {
        return;
    }
    if (endian == Endian::Big) {
        out[offset + 0U] = static_cast<std::uint8_t>((value >> 24U) & 0xFFU);
        out[offset + 1U] = static_cast<std::uint8_t>((value >> 16U) & 0xFFU);
        out[offset + 2U] = static_cast<std::uint8_t>((value >> 8U) & 0xFFU);
        out[offset + 3U] = static_cast<std::uint8_t>(value & 0xFFU);
    } else {
        out[offset + 0U] = static_cast<std::uint8_t>(value & 0xFFU);
        out[offset + 1U] = static_cast<std::uint8_t>((value >> 8U) & 0xFFU);
        out[offset + 2U] = static_cast<std::uint8_t>((value >> 16U) & 0xFFU);
        out[offset + 3U] = static_cast<std::uint8_t>((value >> 24U) & 0xFFU);
    }
}

void writeF32(std::vector<std::uint8_t>& out, const std::size_t offset, const float value, const Endian endian) {
    writeU32(out, offset, std::bit_cast<std::uint32_t>(value), endian);
}

void writeHeader(std::vector<std::uint8_t>& out, const model::MldHeader& header, const Endian endian) {
    if (out.size() < kMldHeaderSize) {
        out.resize(kMldHeaderSize);
    }
    writeU32(out, 0x00U, header.entryCount, endian);
    writeU32(out, 0x04U, header.indexTableOffset, endian);
    writeU32(out, 0x08U, header.functionParametersOffset, endian);
    writeU32(out, 0x0CU, header.realDataOffset, endian);
    writeU32(out, 0x10U, header.textureTableOffset, endian);
}

void writeIndexEntry(std::vector<std::uint8_t>& out,
    const model::MldHeader& header,
    const model::MldIndexEntryRecord& record,
    const Endian endian) {
    const auto offset = static_cast<std::size_t>(header.indexTableOffset) + (record.entry.tableIndex * kEntrySize);
    if (offset > out.size() || kEntrySize > out.size() - offset) {
        return;
    }

    writeU32(out, offset + 0x00U, record.entry.entryId, endian);
    writeU32(out, offset + 0x04U, record.entry.tblId, endian);
    writeU32(out, offset + 0x08U, record.groundLinksPointer, endian);
    writeU32(out, offset + 0x0CU, record.paramList2Pointer, endian);
    writeU32(out, offset + 0x10U, record.functionParametersPointer, endian);
    writeU32(out, offset + 0x14U, record.objectAddressesPointer, endian);
    writeU32(out, offset + 0x18U, record.groundAddressesPointer, endian);
    writeU32(out, offset + 0x1CU, record.motionAddressesPointer, endian);
    writeU32(out, offset + 0x20U, record.entry.texturesPointer, endian);
    writeAscii(out, offset + 0x24U, record.entry.fxnName, 0x14U);
    writeF32(out, offset + 0x44U, record.entry.transform.position.x, endian);
    writeF32(out, offset + 0x48U, record.entry.transform.position.y, endian);
    writeF32(out, offset + 0x4CU, record.entry.transform.position.z, endian);
    writeF32(out, offset + 0x50U, record.entry.transform.rotationRaw.x, endian);
    writeF32(out, offset + 0x54U, record.entry.transform.rotationRaw.y, endian);
    writeF32(out, offset + 0x58U, record.entry.transform.rotationRaw.z, endian);
    writeF32(out, offset + 0x5CU, record.entry.transform.scale.x, endian);
    writeF32(out, offset + 0x60U, record.entry.transform.scale.y, endian);
    writeF32(out, offset + 0x64U, record.entry.transform.scale.z, endian);
}

void writeList(std::vector<std::uint8_t>& out, const model::U32List& list, const Endian endian) {
    const auto offset = static_cast<std::size_t>(list.pointer);
    if (offset > out.size() || 4U > out.size() - offset) {
        return;
    }
    const auto count = static_cast<std::uint32_t>(list.values.size());
    const auto required = 4U + (static_cast<std::size_t>(count) * 4U);
    if (required > out.size() - offset) {
        return;
    }
    writeU32(out, offset, count, endian);
    for (std::size_t i = 0; i < list.values.size(); ++i) {
        writeU32(out, offset + 4U + (i * 4U), list.values[i], endian);
    }
}

[[nodiscard]] std::optional<std::size_t> addRelative(const std::size_t base, const std::int32_t rel, const std::size_t size) {
    const auto target = static_cast<std::int64_t>(base) + static_cast<std::int64_t>(rel);
    if (target < 0 || static_cast<std::uint64_t>(target) > static_cast<std::uint64_t>(size)) {
        return std::nullopt;
    }
    return static_cast<std::size_t>(target);
}

void convertGrndBlock(std::vector<std::uint8_t>& out, const model::MldRawDataBlock& block, const Endian sourceEndian, const Endian targetEndian) {
    if (block.bytes.size() < 0x2CU || static_cast<std::size_t>(block.offset) + block.bytes.size() > out.size()) {
        return;
    }

    EndianReader source(block.bytes, sourceEndian);
    const auto declaredSize = source.try_read_u32(4U).value_or(static_cast<std::uint32_t>(block.bytes.size()));
    const auto size = std::min<std::size_t>(declaredSize, block.bytes.size());
    if (size < 0x2CU) {
        return;
    }

    const auto base = static_cast<std::size_t>(block.offset);
    const auto copyU16 = [&](const std::size_t rel) {
        if (rel + 2U <= size) {
            writeU16(out, base + rel, source.read_u16(rel), targetEndian);
        }
    };
    const auto copyU32 = [&](const std::size_t rel) {
        if (rel + 4U <= size) {
            writeU32(out, base + rel, source.read_u32(rel), targetEndian);
        }
    };

    copyU32(4U);
    constexpr std::size_t innerHeader = 0x10U;
    copyU32(innerHeader);
    copyU32(innerHeader + 4U);
    for (std::size_t rel = innerHeader + 0x10U; rel <= innerHeader + 0x1AU; rel += 2U) {
        copyU16(rel);
    }

    const auto triangleSetsRel = source.try_read_i32(innerHeader);
    const auto quadRegistryRel = source.try_read_i32(innerHeader + 4U);
    const auto triangleSetCount = source.try_read_u16(innerHeader + 0x18U).value_or(0U);
    const auto quadCellCount = source.try_read_u16(innerHeader + 0x1AU).value_or(0U);
    const auto triangleSetsOffset = triangleSetsRel ? addRelative(innerHeader, *triangleSetsRel, size) : std::nullopt;
    const auto quadRegistryOffset = quadRegistryRel ? addRelative(innerHeader, *quadRegistryRel, size) : std::nullopt;

    if (triangleSetsOffset.has_value()) {
        for (std::uint16_t i = 0; i < triangleSetCount; ++i) {
            const auto setOffset = *triangleSetsOffset + (static_cast<std::size_t>(i) * 0x18U);
            if (setOffset + 0x18U > size) {
                break;
            }
            copyU32(setOffset + 0x0CU);
            copyU32(setOffset + 0x10U);
            copyU32(setOffset + 0x14U);

            const auto streamRel = source.try_read_i32(setOffset + 0x10U);
            const auto vertexRel = source.try_read_i32(setOffset + 0x0CU);
            const auto streamOffset = streamRel ? addRelative(setOffset + 0x10U, *streamRel, size) : std::nullopt;
            const auto vertexOffset = vertexRel ? addRelative(setOffset + 0x0CU, *vertexRel, size) : std::nullopt;
            if (streamOffset.has_value() && vertexOffset.has_value() && *streamOffset <= *vertexOffset) {
                for (auto rel = *streamOffset; rel + 4U <= *vertexOffset; rel += 4U) {
                    copyU16(rel);
                    copyU16(rel + 2U);
                }
                for (auto rel = *vertexOffset; rel + 4U <= size; rel += 4U) {
                    copyU32(rel);
                }
            }
        }
    }

    if (quadRegistryOffset.has_value() && *quadRegistryOffset + 4U <= size) {
        copyU32(*quadRegistryOffset);
        const auto tableOffset = *quadRegistryOffset + 4U;
        for (std::uint16_t i = 0; i < quadCellCount; ++i) {
            const auto quadOffset = tableOffset + (static_cast<std::size_t>(i) * 8U);
            if (quadOffset + 8U > size) {
                break;
            }
            copyU32(quadOffset);
            copyU32(quadOffset + 4U);
            const auto refCount = source.try_read_u32(quadOffset).value_or(0U);
            const auto refRel = source.try_read_i32(quadOffset + 4U);
            const auto refOffset = refRel ? addRelative(quadOffset + 4U, *refRel, size) : std::nullopt;
            if (refOffset.has_value()) {
                for (std::uint32_t ref = 0; ref < refCount && *refOffset + (static_cast<std::size_t>(ref) * 4U) + 4U <= size; ++ref) {
                    const auto rel = *refOffset + (static_cast<std::size_t>(ref) * 4U);
                    copyU16(rel);
                    copyU16(rel + 2U);
                }
            }
        }
    }
}

void convertGobjBlock(std::vector<std::uint8_t>& out, const model::MldRawDataBlock& block, const Endian sourceEndian, const Endian targetEndian) {
    if (block.bytes.size() < 0x44U || static_cast<std::size_t>(block.offset) + block.bytes.size() > out.size()) {
        return;
    }
    EndianReader source(block.bytes, sourceEndian);
    const auto declaredSize = source.try_read_u32(4U).value_or(static_cast<std::uint32_t>(block.bytes.size()));
    const auto size = std::min<std::size_t>(declaredSize, block.bytes.size());
    if (size < 0x44U) {
        return;
    }

    const auto base = static_cast<std::size_t>(block.offset);
    const auto copyU16 = [&](const std::size_t rel) {
        if (rel + 2U <= size) {
            writeU16(out, base + rel, source.read_u16(rel), targetEndian);
        }
    };
    const auto copyU32 = [&](const std::size_t rel) {
        if (rel + 4U <= size) {
            writeU32(out, base + rel, source.read_u32(rel), targetEndian);
        }
    };

    copyU32(4U);
    std::vector<std::size_t> stack{ 0x10U };
    std::unordered_set<std::size_t> visited{};
    while (!stack.empty()) {
        const auto node = stack.back();
        stack.pop_back();
        if (!visited.insert(node).second || node + 0x34U > size) {
            continue;
        }
        copyU32(node);
        for (std::size_t rel = node + 8U; rel < node + 0x2CU; rel += 4U) {
            copyU32(rel);
        }
        copyU32(node + 0x2CU);
        copyU32(node + 0x30U);

        const auto attachRel = source.try_read_i32(node);
        if (attachRel.has_value()) {
            if (const auto attach = addRelative(node, *attachRel, size)) {
                const auto payload = *attach + 0x10U;
                if (payload + 4U <= size) {
                    copyU32(payload);
                    const auto vertexRel = source.try_read_i32(payload);
                    if (vertexRel.has_value()) {
                        if (const auto vertex = addRelative(payload, *vertexRel, size)) {
                            for (auto rel = payload + 76U; rel + 4U <= *vertex; rel += 4U) {
                                copyU16(rel);
                                copyU16(rel + 2U);
                            }
                            for (auto rel = *vertex; rel + 4U <= size; rel += 4U) {
                                copyU32(rel);
                            }
                        }
                    }
                }
            }
        }

        const auto childRel = source.try_read_i32(node + 0x2CU);
        if (childRel.has_value()) {
            if (const auto child = addRelative(node + 0x2CU, *childRel, size)) {
                stack.push_back(*child);
            }
        }
        const auto siblingRel = source.try_read_i32(node + 0x30U);
        if (siblingRel.has_value()) {
            if (const auto sibling = addRelative(node + 0x2CU, *siblingRel, size)) {
                stack.push_back(*sibling);
            }
        }
    }
}

void convertRawDataBlocks(std::vector<std::uint8_t>& out, const model::MldFile& file, const Endian targetEndian) {
    if (file.endian == targetEndian) {
        return;
    }
    for (const auto& block : file.rawDataBlocks) {
        switch (block.kind) {
        case model::MldRawDataBlock::Kind::Grnd:
            convertGrndBlock(out, block, file.endian, targetEndian);
            break;
        case model::MldRawDataBlock::Kind::Gobj:
            convertGobjBlock(out, block, file.endian, targetEndian);
            break;
        default:
            break;
        }
    }
}

} // namespace

std::vector<std::uint8_t> MldFileExporter::exportFile(
    const model::MldFile& file,
    const MldExportOptions& options) const {
    auto out = file.originalBytes;
    if (out.empty()) {
        out.resize(kMldHeaderSize);
    }

    const auto targetEndian = endianForPlatform(options.platform);
    writeHeader(out, file.header, targetEndian);
    for (const auto& record : file.entries) {
        writeIndexEntry(out, file.header, record, targetEndian);
    }
    for (const auto& list : file.u32Lists) {
        writeList(out, list, targetEndian);
    }
    convertRawDataBlocks(out, file, targetEndian);

    if (options.compressAklz) {
        if (options.platform != model::TargetPlatform::GameCube) {
            throw std::runtime_error("AKLZ compression is GameCube-only");
        }
        auto compressed = soasim::compression::aklz::compress(out);
        if (!compressed.ok()) {
            throw std::runtime_error("AKLZ compression failed: " + std::string(soasim::compression::aklz::errorToString(compressed.error)));
        }
        return std::move(compressed.bytes);
    }

    return out;
}

} // namespace soasim::mld::exporting
