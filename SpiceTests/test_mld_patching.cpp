#include "../SpiceMLD/SpiceMLD.h"
#include "../SpiceMLD/Internal/MldSha256.h"
#include "../SpiceMLD/Patching/PatchInternals.h"
#include "../SpiceMLD/Model/MldGroundEditing.h"
#include "../SpiceMLD/Model/TriangleMetadata.h"
#include "../SpiceMLD/Patching/TriangleMetadataPatcher.h"
#include "../SpiceMLD/Parsing/MldParser.h"
#include "../SpiceMLD/Parsing/GobjParser.h"
#include "../SpiceMLD/Parsing/GrndParser.h"
#include "../Compression/Aklz.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <bit>
#include <cctype>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <set>
#include <span>
#include <string>
#include <vector>

namespace {

using spice::root::Endian;
using spice::mld::model::MldFile;
using spice::mld::model::MldGroundResource;
using spice::mld::patching::DreamcastTriangleSelectorEdit;
using spice::mld::patching::TriangleSelectorEdit;
using spice::mld::patching::TriangleResourceKind;

constexpr std::size_t kGrndAddress = 0x100U;
constexpr std::size_t kGobjAddress = 0x300U;
constexpr std::size_t kGrndStreamOffset = 0x60U;
constexpr std::size_t kGobjPolyOffset = 0xACU;

void writeU16(std::vector<std::uint8_t>& bytes, const std::size_t offset, const std::uint16_t value,
    const Endian endian = Endian::Little) {
    if (endian == Endian::Big) {
        bytes[offset] = static_cast<std::uint8_t>((value >> 8U) & 0xFFU);
        bytes[offset + 1U] = static_cast<std::uint8_t>(value & 0xFFU);
    } else {
        bytes[offset] = static_cast<std::uint8_t>(value & 0xFFU);
        bytes[offset + 1U] = static_cast<std::uint8_t>((value >> 8U) & 0xFFU);
    }
}
void writeU32(std::vector<std::uint8_t>& bytes, const std::size_t offset, const std::uint32_t value,
    const Endian endian = Endian::Little) {
    if (endian == Endian::Big) {
        bytes[offset] = static_cast<std::uint8_t>((value >> 24U) & 0xFFU);
        bytes[offset + 1U] = static_cast<std::uint8_t>((value >> 16U) & 0xFFU);
        bytes[offset + 2U] = static_cast<std::uint8_t>((value >> 8U) & 0xFFU);
        bytes[offset + 3U] = static_cast<std::uint8_t>(value & 0xFFU);
    } else {
        bytes[offset] = static_cast<std::uint8_t>(value & 0xFFU);
        bytes[offset + 1U] = static_cast<std::uint8_t>((value >> 8U) & 0xFFU);
        bytes[offset + 2U] = static_cast<std::uint8_t>((value >> 16U) & 0xFFU);
        bytes[offset + 3U] = static_cast<std::uint8_t>((value >> 24U) & 0xFFU);
    }
}

void writeF32(std::vector<std::uint8_t>& bytes, const std::size_t offset, const float value,
    const Endian endian = Endian::Little) {
    writeU32(bytes, offset, std::bit_cast<std::uint32_t>(value), endian);
}

void writeTag(std::vector<std::uint8_t>& bytes, const std::size_t offset, const char* tag) {
    for (std::size_t i = 0; i < 4U; ++i) {
        bytes[offset + i] = static_cast<std::uint8_t>(tag[i]);
    }
}

std::vector<std::uint8_t> makeDreamcastGrnd(const Endian endian = Endian::Little, const std::size_t triangleCount = 1) {
    constexpr std::size_t innerHeader = 0x10U;
    constexpr std::size_t triangleSetsOffset = 0x40U;
    const std::size_t vertexOffset = std::max(std::size_t{0x80}, (kGrndStreamOffset + triangleCount * 12 + 15) & ~std::size_t{15});
    const std::size_t quadRegistryOffset = vertexOffset + 72;
    const std::size_t quadTableOffset = quadRegistryOffset + 4;
    const std::size_t refListOffset = quadRegistryOffset + 20;
    const std::size_t declaredSize = refListOffset + triangleCount * 4;

    std::vector<std::uint8_t> bytes(declaredSize, 0U);
    writeTag(bytes, 0U, "GRND");
    writeU32(bytes, 4U, static_cast<std::uint32_t>(declaredSize), endian);
    writeU32(bytes, innerHeader, static_cast<std::uint32_t>(triangleSetsOffset - innerHeader), endian);
    writeU32(bytes, innerHeader + 4U, static_cast<std::uint32_t>(quadRegistryOffset - innerHeader), endian);
    writeU16(bytes, innerHeader + 0x10U, 1U, endian);
    writeU16(bytes, innerHeader + 0x12U, 1U, endian);
    writeU16(bytes, innerHeader + 0x14U, 1U, endian);
    writeU16(bytes, innerHeader + 0x16U, 1U, endian);
    writeU16(bytes, innerHeader + 0x18U, 1U, endian);
    writeU16(bytes, innerHeader + 0x1AU, 1U, endian);

    writeU32(bytes, triangleSetsOffset + 0x0CU,
        static_cast<std::uint32_t>(vertexOffset - (triangleSetsOffset + 0x0CU)), endian);
    writeU32(bytes, triangleSetsOffset + 0x10U,
        static_cast<std::uint32_t>(kGrndStreamOffset - (triangleSetsOffset + 0x10U)), endian);
    writeU32(bytes, triangleSetsOffset + 0x14U, static_cast<std::uint32_t>(triangleCount), endian);
    constexpr std::array<std::uint16_t, 3> flags{ 1U, 2U, 0x800AU };
    for (std::size_t i = 0; i < 3U; ++i) {
        writeU16(bytes, kGrndStreamOffset + i * 4U, static_cast<std::uint16_t>(i * 6U), endian);
        writeU16(bytes, kGrndStreamOffset + i * 4U + 2U, flags[i], endian);
        const auto vertex = vertexOffset + i * 24U;
        writeF32(bytes, vertex + 0U, static_cast<float>(i), endian);
        writeF32(bytes, vertex + 4U, static_cast<float>(i == 1U), endian);
        writeF32(bytes, vertex + 8U, static_cast<float>(i == 2U), endian);
        writeF32(bytes, vertex + 16U, 1.0F, endian);
    }
    for (std::size_t triangle = 1; triangle < triangleCount; ++triangle)
        std::copy_n(bytes.begin() + kGrndStreamOffset, 12, bytes.begin() + kGrndStreamOffset + triangle * 12);
    writeU32(bytes, quadTableOffset, static_cast<std::uint32_t>(triangleCount), endian);
    writeU32(bytes, quadTableOffset + 4U,
        static_cast<std::uint32_t>(refListOffset - (quadTableOffset + 4U)), endian);
    for (std::size_t triangle = 0; triangle < triangleCount; ++triangle) {
        writeU16(bytes, refListOffset + triangle * 4, 0U, endian);
        writeU16(bytes, refListOffset + triangle * 4 + 2, static_cast<std::uint16_t>(triangle * 3), endian);
    }
    return bytes;
}

std::vector<std::uint8_t> makeDreamcastGobj(
    const Endian endian = Endian::Little,
    const bool normalDiffuse = false) {
    constexpr std::size_t nodeOffset = 0x10U;
    constexpr std::size_t attachOffset = 0x50U;
    constexpr std::size_t payloadOffset = attachOffset + 0x10U;
    constexpr std::size_t vertexOffset = 0xC0U;
    constexpr std::size_t vertexCount = 4U;
    const std::size_t recordWords = normalDiffuse ? 7U : 3U;
    const auto declaredSize = vertexOffset + 8U + vertexCount * recordWords * 4U;

    std::vector<std::uint8_t> bytes(declaredSize, 0U);
    writeTag(bytes, 0U, "GOBJ");
    writeU32(bytes, 4U, static_cast<std::uint32_t>(declaredSize), endian);
    writeU32(bytes, nodeOffset, static_cast<std::uint32_t>(attachOffset - nodeOffset), endian);
    writeF32(bytes, nodeOffset + 0x20U, 1.0F, endian);
    writeF32(bytes, nodeOffset + 0x24U, 1.0F, endian);
    writeF32(bytes, nodeOffset + 0x28U, 1.0F, endian);
    writeU32(bytes, payloadOffset, static_cast<std::uint32_t>(vertexOffset - payloadOffset), endian);

    constexpr std::array<std::uint16_t, 4> flags{ 1U, 2U, 0x800AU, 70U };
    for (std::size_t i = 0; i < vertexCount; ++i) {
        writeU16(bytes, kGobjPolyOffset + i * 4U, static_cast<std::uint16_t>(2U + i * recordWords), endian);
        writeU16(bytes, kGobjPolyOffset + i * 4U + 2U, flags[i], endian);
    }
    writeU16(bytes, kGobjPolyOffset + vertexCount * 4U, 0xFFFFU, endian);
    writeU16(bytes, kGobjPolyOffset + vertexCount * 4U + 2U, 0xFFFFU, endian);

    writeU32(bytes, vertexOffset, normalDiffuse ? 0x2AU : 0x22U, endian);
    writeU32(bytes, vertexOffset + 4U, static_cast<std::uint32_t>(vertexCount << 16U), endian);
    for (std::size_t i = 0; i < vertexCount; ++i) {
        const auto vertex = vertexOffset + 8U + i * recordWords * 4U;
        writeF32(bytes, vertex + 0U, static_cast<float>(i), endian);
        writeF32(bytes, vertex + 4U, static_cast<float>(i + 1U), endian);
        writeF32(bytes, vertex + 8U, static_cast<float>(i + 2U), endian);
        if (normalDiffuse) {
            writeF32(bytes, vertex + 12U, 0.0F, endian);
            writeF32(bytes, vertex + 16U, 1.0F, endian);
            writeF32(bytes, vertex + 20U, 0.0F, endian);
            writeU32(bytes, vertex + 24U, 0x10203040U + static_cast<std::uint32_t>(i), endian);
        }
    }
    return bytes;
}

MldFile makeMixedFile(
    const spice::mld::model::TargetPlatform platform,
    const Endian endian,
    const bool compressedAklz,
    const bool normalDiffuseGobj = false) {
    const auto grndBytes = makeDreamcastGrnd(endian);
    const auto gobjBytes = makeDreamcastGobj(endian, normalDiffuseGobj);
    MldFile file{};
    file.parseStatus = spice::mld::model::MldParseStatus::Complete;
    file.sourcePlatform = platform;
    file.endian = endian;
    file.sourceWasCompressedAklz = compressedAklz;
    file.decodedBytes.assign(kGobjAddress + gobjBytes.size() + 0x20U, 0xCCU);
    std::copy(grndBytes.begin(), grndBytes.end(), file.decodedBytes.begin() + static_cast<std::ptrdiff_t>(kGrndAddress));
    std::copy(gobjBytes.begin(), gobjBytes.end(), file.decodedBytes.begin() + static_cast<std::ptrdiff_t>(kGobjAddress));
    file.originalBytes = file.decodedBytes;
    if (compressedAklz) {
        const auto compressed = spice::compression::aklz::compress(file.decodedBytes);
        EXPECT_TRUE(compressed.ok());
        file.sourceBytes = compressed.bytes;
    } else {
        file.sourceBytes = file.decodedBytes;
    }

    auto grnd = spice::mld::parsing::GrndParser{}.decode(
        grndBytes, static_cast<std::uint32_t>(kGrndAddress), endian);
    EXPECT_TRUE(grnd.decoded);
    MldGroundResource grndResource{};
    grndResource.kind = MldGroundResource::Kind::Grnd;
    grndResource.sourceAddress = static_cast<std::uint32_t>(kGrndAddress);
    grndResource.blockSize = grndBytes.size();
    grndResource.tag = "GRND";
    grndResource.rawBytes = grndBytes;
    grndResource.grnd = std::move(grnd.data);
    grndResource.originalSemanticHash = spice::mld::model::semanticHash(*grndResource.grnd);
    file.groundResources.emplace(grndResource.sourceAddress, std::move(grndResource));

    auto gobj = spice::mld::parsing::GobjParser{}.decode(
        gobjBytes, static_cast<std::uint32_t>(kGobjAddress), endian);
    EXPECT_TRUE(gobj.decoded);
    MldGroundResource gobjResource{};
    gobjResource.kind = MldGroundResource::Kind::Gobj;
    gobjResource.sourceAddress = static_cast<std::uint32_t>(kGobjAddress);
    gobjResource.blockSize = gobjBytes.size();
    gobjResource.tag = "GOBJ";
    gobjResource.rawBytes = gobjBytes;
    gobjResource.gobj = std::move(gobj.data);
    gobjResource.originalSemanticHash = spice::mld::model::semanticHash(*gobjResource.gobj);
    file.groundResources.emplace(gobjResource.sourceAddress, std::move(gobjResource));
    return file;
}

MldFile makeMixedDreamcastFile() {
    return makeMixedFile(spice::mld::model::TargetPlatform::Dreamcast, Endian::Little, false);
}

MldFile makeMixedGameCubeFile(const bool compressedAklz) {
    return makeMixedFile(spice::mld::model::TargetPlatform::GameCube, Endian::Big, compressedAklz);
}

std::vector<std::uint8_t> readFile(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
    input.seekg(0, std::ios::end);
    const auto length = input.tellg();
    input.seekg(0, std::ios::beg);
    std::vector<std::uint8_t> bytes(static_cast<std::size_t>(length));
    if (!bytes.empty()) {
        input.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    }
    return bytes;
}

std::uint8_t differentValidSelector(const std::uint16_t rawWord) {
    const auto low = static_cast<std::uint16_t>(rawWord & 0x7FFFU);
    const auto current = static_cast<std::uint16_t>((low / 10U) % 10U);
    for (std::uint16_t digit = 0U; digit <= 9U; ++digit) {
        const auto replacement = static_cast<std::uint32_t>(low) - current * 10U + digit * 10U;
        if (digit != current && replacement <= 0x7FFFU) {
            return static_cast<std::uint8_t>(digit);
        }
    }
    return static_cast<std::uint8_t>(current);
}

void hashWord(std::uint64_t& hash, const std::uint64_t value) {
    for (unsigned shift = 0U; shift < 64U; shift += 8U) {
        hash ^= static_cast<std::uint8_t>(value >> shift);
        hash *= 1099511628211ULL;
    }
}

bool isGroundCorpusMld(const std::filesystem::path& path) {
    auto extension = path.extension().string();
    std::transform(extension.begin(), extension.end(), extension.begin(), [](const unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    if (extension != ".mld") {
        return false;
    }
    auto stem = path.stem().string();
    std::transform(stem.begin(), stem.end(), stem.begin(), [](const unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    if (stem.size() != 5U || stem[0] != 'a' || !std::isdigit(static_cast<unsigned char>(stem[1])) ||
        !std::isdigit(static_cast<unsigned char>(stem[2])) || !std::isdigit(static_cast<unsigned char>(stem[3])) ||
        !std::isalpha(static_cast<unsigned char>(stem[4]))) {
        return false;
    }
    const auto number = static_cast<unsigned>((stem[1] - '0') * 100 + (stem[2] - '0') * 10 + (stem[3] - '0'));
    return number <= 199U;
}

struct GroundCorpusCounts {
    std::size_t files = 0U;
    std::size_t triangles = 0U;
    std::size_t normalDiffuseResources = 0U;
    std::size_t normalDiffuseTriangles = 0U;
    std::size_t selectorEightTriangles = 0U;
    std::size_t noReferenceGrndResources = 0U;
    std::set<std::uint16_t> low15Values{};
};

GroundCorpusCounts scanGroundCorpus(const std::vector<std::filesystem::path>& roots) {
    GroundCorpusCounts counts{};
    for (const auto& root : roots) {
        std::vector<std::filesystem::path> files{};
        for (const auto& item : std::filesystem::directory_iterator(root)) {
            if (item.is_regular_file() && isGroundCorpusMld(item.path())) {
                files.push_back(item.path());
            }
        }
        std::sort(files.begin(), files.end());
        for (const auto& path : files) {
            const auto file = spice::mld::parsing::MldParser{}.parseBytes(readFile(path));
            ++counts.files;
            std::set<std::uint32_t> groundAddresses{};
            for (const auto& record : file.entries) {
                if (record.entry.groundAddresses) {
                    groundAddresses.insert(record.entry.groundAddresses->values.begin(),
                        record.entry.groundAddresses->values.end());
                }
            }
            for (const auto address : groundAddresses) {
                const auto found = file.groundResources.find(address);
                if (found == file.groundResources.end()) {
                    continue;
                }
                const auto& resource = found->second;
                if (resource.grnd.has_value()) {
                    const auto& grnd = *resource.grnd;
                    if (grnd.mesh.triangleMetadata.empty() && grnd.triangleSets.size() == 3U && !grnd.cells.empty()) {
                        ++counts.noReferenceGrndResources;
                    }
                    for (const auto& metadata : grnd.mesh.triangleMetadata) {
                        const auto low15 = static_cast<std::uint16_t>(metadata.rawU16[2] & 0x7FFFU);
                        ++counts.triangles;
                        counts.low15Values.insert(low15);
                        if (((low15 / 10U) % 10U) == 8U) {
                            ++counts.selectorEightTriangles;
                        }
                    }
                }
                if (resource.gobj.has_value()) {
                    bool resourceHasNormalDiffuse = false;
                    std::size_t resourceNormalDiffuseTriangles = 0U;
                    for (const auto& node : resource.gobj->nodes) {
                        const bool nodeNormalDiffuse = node.attach.has_value() &&
                            node.attach->vertexChunk.chunkType == 0x2AU;
                        resourceHasNormalDiffuse = resourceHasNormalDiffuse || nodeNormalDiffuse;
                        if (nodeNormalDiffuse) {
                            resourceNormalDiffuseTriangles += node.streamMesh.triangleMetadata.size();
                        }
                        for (const auto& metadata : node.streamMesh.triangleMetadata) {
                            const auto low15 = static_cast<std::uint16_t>(metadata.rawU16[2] & 0x7FFFU);
                            ++counts.triangles;
                            counts.low15Values.insert(low15);
                            if (((low15 / 10U) % 10U) == 8U) {
                                ++counts.selectorEightTriangles;
                            }
                        }
                    }
                    if (resourceHasNormalDiffuse) {
                        ++counts.normalDiffuseResources;
                        counts.normalDiffuseTriangles += resourceNormalDiffuseTriangles;
                    }
                }
            }
        }
    }
    return counts;
}

} // namespace

TEST(MldPatching, ParsersRecordExactTriangleMetadataOffsets) {
    const auto grndBytes = makeDreamcastGrnd();
    const auto grnd = spice::mld::parsing::GrndParser{}.decode(
        grndBytes, static_cast<std::uint32_t>(kGrndAddress), Endian::Little);
    ASSERT_EQ(grnd.data.triangleSources.size(), 1U);
    EXPECT_EQ(grnd.data.triangleSources[0].triangleSet, 0U);
    EXPECT_EQ(grnd.data.triangleSources[0].streamIndex, 0U);
    EXPECT_EQ(grnd.data.triangleSources[0].flagSourceOffsets,
        (std::array<std::size_t, 3>{ kGrndAddress + kGrndStreamOffset + 2U,
            kGrndAddress + kGrndStreamOffset + 6U,
            kGrndAddress + kGrndStreamOffset + 10U }));

    const auto gobjBytes = makeDreamcastGobj();
    const auto gobj = spice::mld::parsing::GobjParser{}.decode(
        gobjBytes, static_cast<std::uint32_t>(kGobjAddress), Endian::Little);
    ASSERT_EQ(gobj.data.nodes.size(), 1U);
    ASSERT_EQ(gobj.data.nodes[0].streamTriangleSources.size(), 2U);
    EXPECT_EQ(gobj.data.nodes[0].streamTriangleSources[0].flagSourceOffsets[2],
        kGobjAddress + kGobjPolyOffset + 10U);
    EXPECT_EQ(gobj.data.nodes[0].streamTriangleSources[1].flagSourceOffsets[2],
        kGobjAddress + kGobjPolyOffset + 14U);
}

TEST(MldPatching, PlansAndAppliesMixedGrndAndGobjSelectorEdits) {
    const auto file = makeMixedDreamcastFile();
    const std::array edits{
        DreamcastTriangleSelectorEdit{
            .resourceKind = TriangleResourceKind::Grnd,
            .resourceAddress = static_cast<std::uint32_t>(kGrndAddress),
            .triangleIndex = 0U,
            .selectorDigit = 7U,
        },
        DreamcastTriangleSelectorEdit{
            .resourceKind = TriangleResourceKind::Gobj,
            .resourceAddress = static_cast<std::uint32_t>(kGobjAddress),
            .gobjNodeIndex = 0U,
            .triangleIndex = 0U,
            .selectorDigit = 9U,
        },
    };
    const auto plan = spice::mld::patching::planDreamcastTriangleSelectorPatches(file, edits);
    ASSERT_TRUE(plan.ok());
    ASSERT_EQ(plan.patches.size(), 2U);

    auto patched = file.sourceBytes;
    const auto applied = spice::mld::patching::applyMldPatchPlan(patched, plan);
    ASSERT_TRUE(applied.ok());
    EXPECT_EQ(applied.appliedPatchCount, 2U);
    EXPECT_EQ(patched.size(), file.sourceBytes.size());
    for (std::size_t i = 0; i < patched.size(); ++i) {
        const bool inPatch = std::any_of(plan.patches.begin(), plan.patches.end(), [&](const auto& patch) {
            return i >= patch.decodedPayloadOffset && i < patch.decodedPayloadOffset + 2U;
        });
        if (!inPatch) {
            EXPECT_EQ(patched[i], file.sourceBytes[i]) << "unexpected change at " << i;
        }
    }

    const auto reparsedGrnd = spice::mld::parsing::GrndParser{}.decode(
        std::span<const std::uint8_t>(patched).subspan(kGrndAddress, makeDreamcastGrnd().size()),
        static_cast<std::uint32_t>(kGrndAddress), Endian::Little);
    ASSERT_EQ(reparsedGrnd.data.mesh.triangleMetadata.size(), 1U);
    EXPECT_EQ((reparsedGrnd.data.mesh.triangleMetadata[0].rawU16[2] >> 15U) & 1U, 1U);
    EXPECT_EQ(((reparsedGrnd.data.mesh.triangleMetadata[0].rawU16[2] & 0x7FFFU) / 10U) % 10U, 7U);

    const auto reparsedGobj = spice::mld::parsing::GobjParser{}.decode(
        std::span<const std::uint8_t>(patched).subspan(kGobjAddress, makeDreamcastGobj().size()),
        static_cast<std::uint32_t>(kGobjAddress), Endian::Little);
    ASSERT_EQ(reparsedGobj.data.nodes[0].streamMesh.triangleMetadata.size(), 2U);
    EXPECT_EQ(((reparsedGobj.data.nodes[0].streamMesh.triangleMetadata[0].rawU16[2] & 0x7FFFU) / 10U) % 10U, 9U);
    EXPECT_EQ(((reparsedGobj.data.nodes[0].streamMesh.triangleMetadata[1].rawU16[2] & 0x7FFFU) / 10U) % 10U, 7U);
}

TEST(MldPatching, PatchesNormalDiffuseGobjWithoutChangingVertexColors) {
    const auto file = makeMixedFile(
        spice::mld::model::TargetPlatform::Dreamcast, Endian::Little, false, true);
    const auto& beforeMesh = file.groundResources.at(static_cast<std::uint32_t>(kGobjAddress))
        .gobj->nodes[0].streamMesh;
    ASSERT_FALSE(beforeMesh.vertices.empty());
    ASSERT_TRUE(beforeMesh.vertices[0].diffuseColor.has_value());

    const TriangleSelectorEdit edit{
        .resourceKind = TriangleResourceKind::Gobj,
        .resourceAddress = static_cast<std::uint32_t>(kGobjAddress),
        .gobjNodeIndex = 0U,
        .triangleIndex = 0U,
        .selectorDigit = 8U,
    };
    const auto plan = spice::mld::patching::planTriangleSelectorPatches(file, std::span{ &edit, 1U });
    ASSERT_TRUE(plan.ok());
    ASSERT_EQ(plan.patches.size(), 1U);
    const auto materialized = spice::mld::patching::materializeMldPatchPlan(file.sourceBytes, plan);
    ASSERT_TRUE(materialized.ok());
    for (std::size_t i = 0; i < materialized.bytes.size(); ++i) {
        const bool changed = i >= plan.patches[0].decodedPayloadOffset &&
            i < plan.patches[0].decodedPayloadOffset + 2U;
        if (!changed) {
            EXPECT_EQ(materialized.bytes[i], file.sourceBytes[i]);
        }
    }

    const auto gobjBytes = makeDreamcastGobj(Endian::Little, true);
    const auto reparsed = spice::mld::parsing::GobjParser{}.decode(
        std::span<const std::uint8_t>(materialized.bytes).subspan(kGobjAddress, gobjBytes.size()),
        static_cast<std::uint32_t>(kGobjAddress), Endian::Little);
    ASSERT_FALSE(reparsed.data.nodes.empty());
    const auto& afterMesh = reparsed.data.nodes[0].streamMesh;
    ASSERT_EQ(afterMesh.vertices.size(), beforeMesh.vertices.size());
    for (std::size_t i = 0; i < afterMesh.vertices.size(); ++i) {
        ASSERT_TRUE(afterMesh.vertices[i].diffuseColor.has_value());
        ASSERT_TRUE(beforeMesh.vertices[i].diffuseColor.has_value());
        EXPECT_EQ(afterMesh.vertices[i].diffuseColor->r, beforeMesh.vertices[i].diffuseColor->r);
        EXPECT_EQ(afterMesh.vertices[i].diffuseColor->g, beforeMesh.vertices[i].diffuseColor->g);
        EXPECT_EQ(afterMesh.vertices[i].diffuseColor->b, beforeMesh.vertices[i].diffuseColor->b);
        EXPECT_EQ(afterMesh.vertices[i].diffuseColor->a, beforeMesh.vertices[i].diffuseColor->a);
    }
    EXPECT_EQ(spice::mld::model::decodeTriangleMetadataWord(
        afterMesh.triangleMetadata[0].rawU16[2]).tensDigit, 8U);
}

TEST(MldPatching, PlansAndMaterializesBigEndianGameCubeEdits) {
    const auto file = makeMixedGameCubeFile(false);
    const TriangleSelectorEdit edit{
        .resourceKind = TriangleResourceKind::Grnd,
        .resourceAddress = static_cast<std::uint32_t>(kGrndAddress),
        .triangleIndex = 0U,
        .selectorDigit = 8U,
    };
    const auto plan = spice::mld::patching::planTriangleSelectorPatches(
        file, std::span{ &edit, 1U });
    ASSERT_TRUE(plan.ok());
    ASSERT_EQ(plan.patches.size(), 1U);
    EXPECT_EQ(plan.endian, Endian::Big);
    EXPECT_FALSE(plan.sourceWasCompressedAklz);
    EXPECT_EQ(plan.patches[0].expectedBytes, (std::array<std::uint8_t, 2>{ 0x80U, 0x0AU }));
    EXPECT_EQ(plan.patches[0].replacementBytes, (std::array<std::uint8_t, 2>{ 0x80U, 0x50U }));

    const auto materialized = spice::mld::patching::materializeMldPatchPlan(file.sourceBytes, plan);
    ASSERT_TRUE(materialized.ok());
    EXPECT_EQ(materialized.appliedPatchCount, 1U);
    EXPECT_EQ(materialized.bytes.size(), file.sourceBytes.size());
    for (std::size_t i = 0; i < materialized.bytes.size(); ++i) {
        const bool changed = i >= plan.patches[0].decodedPayloadOffset &&
            i < plan.patches[0].decodedPayloadOffset + 2U;
        if (!changed) {
            EXPECT_EQ(materialized.bytes[i], file.sourceBytes[i]);
        }
    }

    const auto reparsed = spice::mld::parsing::GrndParser{}.decode(
        std::span<const std::uint8_t>(materialized.bytes).subspan(kGrndAddress, makeDreamcastGrnd(Endian::Big).size()),
        static_cast<std::uint32_t>(kGrndAddress), Endian::Big);
    ASSERT_EQ(reparsed.data.mesh.triangleMetadata.size(), 1U);
    EXPECT_EQ(spice::mld::model::decodeTriangleMetadataWord(
        reparsed.data.mesh.triangleMetadata[0].rawU16[2]).tensDigit, 8U);
}

TEST(MldPatching, MaterializesAklzGameCubeEditsAndPreservesNoOpsExactly) {
    const auto file = makeMixedGameCubeFile(true);
    TriangleSelectorEdit edit{
        .resourceKind = TriangleResourceKind::Gobj,
        .resourceAddress = static_cast<std::uint32_t>(kGobjAddress),
        .gobjNodeIndex = 0U,
        .triangleIndex = 0U,
        .selectorDigit = 9U,
    };
    const auto plan = spice::mld::patching::planTriangleSelectorPatches(
        file, std::span{ &edit, 1U });
    ASSERT_TRUE(plan.ok());
    ASSERT_EQ(plan.patches.size(), 1U);
    EXPECT_TRUE(plan.sourceWasCompressedAklz);

    const auto materialized = spice::mld::patching::materializeMldPatchPlan(file.sourceBytes, plan);
    ASSERT_TRUE(materialized.ok());
    ASSERT_TRUE(spice::compression::aklz::isAklz(materialized.bytes));
    const auto decoded = spice::compression::aklz::decompress(materialized.bytes);
    ASSERT_TRUE(decoded.ok());
    ASSERT_EQ(decoded.bytes.size(), file.decodedBytes.size());
    for (std::size_t i = 0; i < decoded.bytes.size(); ++i) {
        const bool changed = i >= plan.patches[0].decodedPayloadOffset &&
            i < plan.patches[0].decodedPayloadOffset + 2U;
        if (!changed) {
            EXPECT_EQ(decoded.bytes[i], file.decodedBytes[i]);
        }
    }

    const auto reparsed = spice::mld::parsing::GobjParser{}.decode(
        std::span<const std::uint8_t>(decoded.bytes).subspan(kGobjAddress, makeDreamcastGobj(Endian::Big).size()),
        static_cast<std::uint32_t>(kGobjAddress), Endian::Big);
    ASSERT_FALSE(reparsed.data.nodes.empty());
    EXPECT_EQ(spice::mld::model::decodeTriangleMetadataWord(
        reparsed.data.nodes[0].streamMesh.triangleMetadata[0].rawU16[2]).tensDigit, 9U);

    edit.resourceKind = TriangleResourceKind::Grnd;
    edit.resourceAddress = static_cast<std::uint32_t>(kGrndAddress);
    edit.gobjNodeIndex.reset();
    edit.selectorDigit = 1U;
    const auto noOp = spice::mld::patching::planTriangleSelectorPatches(file, std::span{ &edit, 1U });
    ASSERT_TRUE(noOp.ok());
    ASSERT_TRUE(noOp.patches.empty());
    const auto unchanged = spice::mld::patching::materializeMldPatchPlan(file.sourceBytes, noOp);
    ASSERT_TRUE(unchanged.ok());
    EXPECT_EQ(unchanged.bytes, file.sourceBytes);
}

TEST(MldPatching, RejectsStaleDecodedBytesInsideAklzSource) {
    const auto file = makeMixedGameCubeFile(true);
    const TriangleSelectorEdit edit{
        .resourceKind = TriangleResourceKind::Grnd,
        .resourceAddress = static_cast<std::uint32_t>(kGrndAddress),
        .triangleIndex = 0U,
        .selectorDigit = 8U,
    };
    const auto plan = spice::mld::patching::planTriangleSelectorPatches(file, std::span{ &edit, 1U });
    ASSERT_TRUE(plan.ok());
    auto staleDecoded = file.decodedBytes;
    staleDecoded[plan.patches[0].decodedPayloadOffset] ^= 0x01U;
    const auto staleCompressed = spice::compression::aklz::compress(staleDecoded);
    ASSERT_TRUE(staleCompressed.ok());
    const auto materialized = spice::mld::patching::materializeMldPatchPlan(staleCompressed.bytes, plan);
    EXPECT_FALSE(materialized.ok());
    EXPECT_TRUE(materialized.bytes.empty());
    EXPECT_EQ(materialized.appliedPatchCount, 0U);
}

TEST(MldPatching, AllowsEverySelectorDigitWithoutAreaOrResourcePolicy) {
    const auto file = makeMixedDreamcastFile();
    for (std::uint8_t digit = 0U; digit <= 9U; ++digit) {
        const DreamcastTriangleSelectorEdit edit{
            .resourceKind = TriangleResourceKind::Gobj,
            .resourceAddress = static_cast<std::uint32_t>(kGobjAddress),
            .gobjNodeIndex = 0U,
            .triangleIndex = 0U,
            .selectorDigit = digit,
        };
        const auto plan = spice::mld::patching::planDreamcastTriangleSelectorPatches(file, std::span{ &edit, 1U });
        EXPECT_TRUE(plan.ok()) << "selector digit " << static_cast<unsigned>(digit);
    }
}

TEST(MldPatching, DeduplicatesIdenticalEditsAndRejectsConflicts) {
    const auto file = makeMixedDreamcastFile();
    const DreamcastTriangleSelectorEdit first{
        .resourceKind = TriangleResourceKind::Gobj,
        .resourceAddress = static_cast<std::uint32_t>(kGobjAddress),
        .gobjNodeIndex = 0U,
        .triangleIndex = 0U,
        .selectorDigit = 4U,
    };
    const std::array duplicates{ first, first };
    const auto duplicatePlan = spice::mld::patching::planDreamcastTriangleSelectorPatches(file, duplicates);
    ASSERT_TRUE(duplicatePlan.ok());
    EXPECT_EQ(duplicatePlan.patches.size(), 1U);

    auto second = first;
    second.selectorDigit = 5U;
    const std::array conflicts{ first, second };
    const auto conflictPlan = spice::mld::patching::planDreamcastTriangleSelectorPatches(file, conflicts);
    EXPECT_FALSE(conflictPlan.ok());
}

TEST(MldPatching, OmitsNoOpsAndRejectsLow15Overflow) {
    auto file = makeMixedDreamcastFile();
    DreamcastTriangleSelectorEdit edit{
        .resourceKind = TriangleResourceKind::Grnd,
        .resourceAddress = static_cast<std::uint32_t>(kGrndAddress),
        .triangleIndex = 0U,
        .selectorDigit = 1U,
    };
    const auto noOpPlan = spice::mld::patching::planDreamcastTriangleSelectorPatches(
        file, std::span{ &edit, 1U });
    ASSERT_TRUE(noOpPlan.ok());
    EXPECT_TRUE(noOpPlan.patches.empty());

    auto& resource = file.groundResources.at(static_cast<std::uint32_t>(kGrndAddress));
    auto& grnd = *resource.grnd;
    const auto absoluteOffset = grnd.triangleSources[0].flagSourceOffsets[2];
    const auto resourceOffset = absoluteOffset - resource.sourceAddress;
    grnd.mesh.triangleMetadata[0].rawU16[2] = 0x7FFFU;
    writeU16(file.sourceBytes, absoluteOffset, 0x7FFFU);
    writeU16(file.decodedBytes, absoluteOffset, 0x7FFFU);
    writeU16(resource.rawBytes, resourceOffset, 0x7FFFU);
    edit.selectorDigit = 9U;
    EXPECT_FALSE(spice::mld::patching::planDreamcastTriangleSelectorPatches(
        file, std::span{ &edit, 1U }).ok());
}

TEST(MldPatching, AppliesAtomicallyWhenExpectedBytesAreStale) {
    const auto file = makeMixedDreamcastFile();
    const std::array edits{
        DreamcastTriangleSelectorEdit{
            .resourceKind = TriangleResourceKind::Grnd,
            .resourceAddress = static_cast<std::uint32_t>(kGrndAddress),
            .triangleIndex = 0U,
            .selectorDigit = 7U,
        },
        DreamcastTriangleSelectorEdit{
            .resourceKind = TriangleResourceKind::Gobj,
            .resourceAddress = static_cast<std::uint32_t>(kGobjAddress),
            .gobjNodeIndex = 0U,
            .triangleIndex = 0U,
            .selectorDigit = 8U,
        },
    };
    const auto plan = spice::mld::patching::planDreamcastTriangleSelectorPatches(file, edits);
    ASSERT_TRUE(plan.ok());
    ASSERT_EQ(plan.patches.size(), 2U);

    auto stale = file.sourceBytes;
    stale[plan.patches[1].decodedPayloadOffset] ^= 0x01U;
    const auto before = stale;
    const auto applied = spice::mld::patching::applyMldPatchPlan(stale, plan);
    EXPECT_FALSE(applied.ok());
    EXPECT_EQ(applied.appliedPatchCount, 0U);
    EXPECT_EQ(stale, before);
}

TEST(MldPatching, RejectsUnsupportedFilesAndStructurallyInvalidEdits) {
    auto file = makeMixedDreamcastFile();
    const DreamcastTriangleSelectorEdit edit{
        .resourceKind = TriangleResourceKind::Grnd,
        .resourceAddress = static_cast<std::uint32_t>(kGrndAddress),
        .triangleIndex = 0U,
        .selectorDigit = 10U,
    };
    EXPECT_FALSE(spice::mld::patching::planDreamcastTriangleSelectorPatches(file, std::span{ &edit, 1U }).ok());

    file.sourcePlatform = spice::mld::model::TargetPlatform::GameCube;
    file.endian = Endian::Big;
    EXPECT_FALSE(spice::mld::patching::planDreamcastTriangleSelectorPatches(file, {}).ok());

    file.sourcePlatform = spice::mld::model::TargetPlatform::Dreamcast;
    file.endian = Endian::Little;
    file.sourceWasCompressedAklz = true;
    EXPECT_FALSE(spice::mld::patching::planDreamcastTriangleSelectorPatches(file, {}).ok());
}

TEST(MldPatching, RejectsWrongResourceShapeAndOverlappingPatchRecords) {
    const auto file = makeMixedDreamcastFile();
    const std::array invalidEdits{
        DreamcastTriangleSelectorEdit{
            .resourceKind = TriangleResourceKind::Grnd,
            .resourceAddress = static_cast<std::uint32_t>(kGrndAddress),
            .gobjNodeIndex = 0U,
            .triangleIndex = 0U,
            .selectorDigit = 2U,
        },
        DreamcastTriangleSelectorEdit{
            .resourceKind = TriangleResourceKind::Gobj,
            .resourceAddress = static_cast<std::uint32_t>(kGobjAddress),
            .triangleIndex = 0U,
            .selectorDigit = 2U,
        },
    };
    EXPECT_FALSE(spice::mld::patching::planDreamcastTriangleSelectorPatches(file, invalidEdits).ok());

    spice::mld::patching::MldPatchPlan overlap{};
    overlap.patches.push_back(spice::mld::patching::MldBytePatch{
        .decodedPayloadOffset = 4U,
        .expectedBytes = { 0U, 0U },
        .replacementBytes = { 1U, 0U },
    });
    overlap.patches.push_back(spice::mld::patching::MldBytePatch{
        .decodedPayloadOffset = 5U,
        .expectedBytes = { 0U, 0U },
        .replacementBytes = { 2U, 0U },
    });
    std::vector<std::uint8_t> bytes(8U, 0U);
    const auto before = bytes;
    EXPECT_FALSE(spice::mld::patching::applyMldPatchPlan(bytes, overlap).ok());
    EXPECT_EQ(bytes, before);
}

TEST(MldPatching, RejectsTriangleWordThatDoesNotMatchSourceBytes) {
    auto file = makeMixedDreamcastFile();
    auto& grnd = *file.groundResources.at(static_cast<std::uint32_t>(kGrndAddress)).grnd;
    grnd.mesh.triangleMetadata[0].rawU16[2] ^= 1U;
    const DreamcastTriangleSelectorEdit edit{
        .resourceKind = TriangleResourceKind::Grnd,
        .resourceAddress = static_cast<std::uint32_t>(kGrndAddress),
        .triangleIndex = 0U,
        .selectorDigit = 2U,
    };
    EXPECT_FALSE(spice::mld::patching::planDreamcastTriangleSelectorPatches(
        file, std::span{ &edit, 1U }).ok());
}


namespace {
using namespace spice::mld::patching;
constexpr std::size_t kParameterPointer = 0x200;
constexpr std::size_t kParameterEntry = 0x20 + 0x68;

std::vector<std::uint8_t> encounterSource(Endian endian, bool compressed = false) {
    auto bytes = makeMixedFile(endian == Endian::Big ? spice::mld::model::TargetPlatform::GameCube
        : spice::mld::model::TargetPlatform::Dreamcast, endian, false).sourceBytes;
    std::fill(bytes.begin(), bytes.begin() + 0x100, 0);
    writeU32(bytes, 0, 2, endian);
    writeU32(bytes, 4, 0x20, endian);
    writeU32(bytes, 8, kParameterPointer, endian);
    writeU32(bytes, 12, kGrndAddress, endian);
    writeU32(bytes, 16, static_cast<std::uint32_t>(bytes.size()), endian);
    for (const auto offset : {std::size_t{0x20}, kParameterEntry})
        for (std::size_t scale = 0x5c; scale < 0x68; scale += 4) writeF32(bytes, offset + scale, 1, endian);
    const std::string groundName = "ground", parameterName = "testControl";
    std::copy(groundName.begin(), groundName.end(), bytes.begin() + 0x20 + 0x24);
    std::copy(parameterName.begin(), parameterName.end(), bytes.begin() + kParameterEntry + 0x24);
    writeU32(bytes, 0x20, 11, endian);
    writeU32(bytes, 0x20 + 0x18, 0x220, endian);
    writeU32(bytes, 0x220, 2, endian);
    writeU32(bytes, 0x224, kGrndAddress, endian);
    writeU32(bytes, 0x228, kGobjAddress, endian);
    writeU32(bytes, kParameterEntry, 77, endian);
    writeU32(bytes, kParameterEntry + 4, 123, endian);
    writeU32(bytes, kParameterEntry + 0x10, kParameterPointer, endian);
    writeU32(bytes, kParameterPointer, 3, endian);
    writeU32(bytes, kParameterPointer + 4, 0x11223344, endian);
    writeU32(bytes, kParameterPointer + 8, 0, endian);
    writeU32(bytes, kParameterPointer + 12, 0xffffffff, endian);
    return compressed ? spice::compression::aklz::compress(bytes).bytes : bytes;
}
MldFile encounterFile(Endian endian = Endian::Little, bool compressed = false) {
    return spice::mld::parsing::MldParser{}.parseBytes(encounterSource(endian, compressed));
}
MldEncounterPatchRequest encounterRequest(const MldFile& file) {
    return {.sourceSha256 = spice::mld::detail::sha256(file.sourceBytes), .sourceSize = file.sourceBytes.size()};
}
MldFunctionParameterEdit parameterEdit(std::size_t index = 0) {
    constexpr std::array<std::uint32_t, 3> values{0x11223344, 0, 0xffffffff};
    return {.entryTableIndex = 1, .expectedEntryId = 77, .expectedTableId = 123,
        .expectedFunctionName = "testControl", .expectedParameterCount = 3, .parameterIndex = index,
        .expectedValue = values[index], .replacementValue = 0x89abcdef};
}
std::string encounterDiagnostics(const MldEncounterPatchPlan& plan) {
    std::string text;
    for (const auto& d : plan.diagnostics()) text += d.message + "\n";
    return text;
}
void expectRejected(const MldFile& file, const MldEncounterPatchRequest& request) {
    const auto plan = planEncounterPatches(file, request);
    EXPECT_FALSE(plan.ok());
    EXPECT_FALSE(plan.diagnostics().empty());
    const auto result = materializeEncounterPatchPlan(file.sourceBytes, plan);
    EXPECT_FALSE(result.ok());
    EXPECT_TRUE(result.bytes.empty());
    EXPECT_EQ(result.appliedPatchCount, 0);
}
}

TEST(MldEncounterPatching, MaterializesAllEditCombinationsAndPreservesEveryOtherByte) {
    for (int encoding = 0; encoding < 3; ++encoding) {
        const auto endian = encoding == 0 ? Endian::Little : Endian::Big;
        const bool compressed = encoding == 2;
        const auto file = encounterFile(endian, compressed);
        ASSERT_EQ(file.parseStatus, spice::mld::model::MldParseStatus::Complete);
        for (int mode = 0; mode < 3; ++mode) {
            SCOPED_TRACE(std::to_string(encoding) + "/" + std::to_string(mode));
            auto request = encounterRequest(file);
            auto expected = file.decodedBytes;
            if (mode != 0) {
                request.parameterEdits = {parameterEdit(0), parameterEdit(2)};
                writeU32(expected, kParameterPointer + 4, 0x89abcdef, endian);
                writeU32(expected, kParameterPointer + 12, 0x89abcdef, endian);
            }
            if (mode != 1) {
                request.triangleEdits = {{TriangleResourceKind::Grnd, kGrndAddress, {}, 0, 7},
                    {TriangleResourceKind::Gobj, kGobjAddress, 0, 0, 9}};
                writeU16(expected, kGrndAddress + kGrndStreamOffset + 10, 0x8046, endian);
                writeU16(expected, kGobjAddress + kGobjPolyOffset + 10, 0x805a, endian);
            }
            const auto plan = planEncounterPatches(file, request);
            ASSERT_TRUE(plan.ok()) << encounterDiagnostics(plan);
            const auto result = materializeEncounterPatchPlan(file.sourceBytes, plan);
            ASSERT_TRUE(result.ok());
            EXPECT_EQ(result.appliedPatchCount, mode == 2 ? 4 : 2);
            EXPECT_EQ(spice::compression::aklz::isAklz(result.bytes), compressed);
            const auto decoded = compressed ? spice::compression::aklz::decompress(result.bytes).bytes : result.bytes;
            EXPECT_EQ(decoded, expected);
            EXPECT_EQ(file.sourceBytes, encounterSource(endian, compressed));
        }
    }
}

TEST(MldEncounterPatching, RejectsSourceFingerprintDriftIncludingNoOpsAndEmptyPlans) {
    for (const bool compressed : {false, true}) {
        const auto file = encounterFile(Endian::Big, compressed);
        for (int mode = 0; mode < 3; ++mode) {
            auto request = encounterRequest(file);
            if (mode) request.parameterEdits.push_back(parameterEdit());
            if (mode == 1) request.parameterEdits[0].replacementValue = request.parameterEdits[0].expectedValue;
            const auto plan = planEncounterPatches(file, request);
            ASSERT_TRUE(plan.ok()) << encounterDiagnostics(plan);
            const auto unchanged = materializeEncounterPatchPlan(file.sourceBytes, plan);
            ASSERT_TRUE(unchanged.ok());
            if (mode != 2) EXPECT_EQ(unchanged.bytes, file.sourceBytes);
            auto stale = file.sourceBytes;
            stale.back() ^= 1;
            const auto result = materializeEncounterPatchPlan(stale, plan);
            EXPECT_FALSE(result.ok()); EXPECT_TRUE(result.bytes.empty()); EXPECT_EQ(result.appliedPatchCount, 0);
            auto wrongHash = request; wrongHash.sourceSha256[0] ^= 1; expectRejected(file, wrongHash);
            auto wrongSize = request; ++wrongSize.sourceSize; expectRejected(file, wrongSize);
        }
    }
    EXPECT_FALSE(MldEncounterPatchPlan{}.ok());
    EXPECT_FALSE(materializeEncounterPatchPlan({}, MldEncounterPatchPlan{}).ok());
}

TEST(MldEncounterPatching, RejectsInvalidParameterIdentitiesCountsIndicesAndValuesAtomically) {
    const auto file = encounterFile();
    auto good = encounterRequest(file);
    good.triangleEdits.push_back({TriangleResourceKind::Grnd, kGrndAddress, {}, 0, 7});
    good.parameterEdits.push_back(parameterEdit());
    for (int fault = 0; fault < 7; ++fault) {
        auto request = good; auto& e = request.parameterEdits[0];
        switch (fault) {
        case 0: e.entryTableIndex = 99; break;
        case 1: ++e.expectedEntryId; break;
        case 2: ++e.expectedTableId; break;
        case 3: e.expectedFunctionName = "other"; break;
        case 4: ++e.expectedParameterCount; break;
        case 5: e.parameterIndex = 3; break;
        case 6: ++e.expectedValue; break;
        }
        expectRejected(file, request);
    }
}

TEST(MldEncounterPatching, DetectsDuplicateConflictsBeforeDiscardingNoOps) {
    const auto file = encounterFile();
    auto request = encounterRequest(file);
    request.parameterEdits = {parameterEdit(), parameterEdit()};
    request.triangleEdits = {{TriangleResourceKind::Grnd, kGrndAddress, {}, 0, 7},
        {TriangleResourceKind::Grnd, kGrndAddress, {}, 0, 7}};
    auto plan = planEncounterPatches(file, request);
    ASSERT_TRUE(plan.ok()) << encounterDiagnostics(plan);
    EXPECT_EQ(materializeEncounterPatchPlan(file.sourceBytes, plan).appliedPatchCount, 2);
    auto bad = request; bad.parameterEdits[1].replacementValue = bad.parameterEdits[1].expectedValue;
    expectRejected(file, bad);
    bad = request; bad.triangleEdits[1].selectorDigit = 1; expectRejected(file, bad);
    bad = request; ++bad.parameterEdits[1].replacementValue; expectRejected(file, bad);
    bad = request; bad.triangleEdits[1].selectorDigit = 8; expectRejected(file, bad);
}

TEST(MldEncounterPatching, RejectsUnusableTargetBoundsAndExpectedBytes) {
    for (int fault = 0; fault < 7; ++fault) {
        auto file = encounterFile();
        auto request = encounterRequest(file);
        request.parameterEdits.push_back(parameterEdit());
        request.triangleEdits.push_back({TriangleResourceKind::Grnd, kGrndAddress, {}, 0, 7});
        switch (fault) {
        case 0: file.decodedBytes[kParameterPointer + 4] ^= 1; break;
        case 1: file.header.indexTableOffset = 0xffffffff; break;
        case 2: ++file.entries[1].functionParametersPointer; break;
        case 3: ++file.entries[1].entry.functionParameters->values[0]; break;
        case 4: ++file.entries[1].entry.functionParameters->declaredCount.value(); break;
        case 5: file.groundResources.at(kGrndAddress).grnd->triangleSources[0].flagSourceOffsets[2] = file.decodedBytes.size(); break;
        case 6: request.triangleEdits[0].resourceKind = static_cast<TriangleResourceKind>(255); break;
        }
        expectRejected(file, request);
    }
}

TEST(MldEncounterPatching, ResolvesNativeTableIndexAfterParsedVectorReordering) {
    auto file = encounterFile();
    auto request = encounterRequest(file); request.parameterEdits.push_back(parameterEdit());
    std::reverse(file.entries.begin(), file.entries.end());
    EXPECT_TRUE(planEncounterPatches(file, request).ok());
}

TEST(MldEncounterPatching, RejectsSharedAndPartiallyOverlappingListsIncludingOtherRoles) {
    for (const std::size_t field : {0x08, 0x0c, 0x10, 0x14, 0x18, 0x1c}) {
        for (const std::size_t owner : {std::size_t{0x20}, kParameterEntry}) {
            if (owner == kParameterEntry && field == 0x10) continue;
            for (const std::size_t pointer : {kParameterPointer, kParameterPointer + 8}) {
                auto source = encounterSource(Endian::Little);
                // The second word is zero, so a pointer into it is a valid empty list overlapping the target.
                writeU32(source, owner + field, static_cast<std::uint32_t>(pointer));
                const auto file = spice::mld::parsing::MldParser{}.parseBytes(source);
                auto request = encounterRequest(file); request.parameterEdits.push_back(parameterEdit());
                expectRejected(file, request);
            }
        }
    }
}

TEST(MldEncounterPatching, RejectsAbsentTruncatedAndOutOfBoundsParameterLists) {
    for (const std::uint32_t pointer : {0U, 0xfffffff0U, 0x201U}) {
        auto source = encounterSource(Endian::Little);
        writeU32(source, kParameterEntry + 0x10, pointer);
        const auto file = spice::mld::parsing::MldParser{}.parseBytes(source);
        // Malformed documents need not import successfully; fingerprint the actual source directly.
        auto request = encounterRequest(encounterFile());
        request.sourceSha256 = spice::mld::detail::sha256(source);
        request.sourceSize = source.size(); request.parameterEdits.push_back(parameterEdit());
        expectRejected(file, request);
    }
}

TEST(MldEncounterPatching, RejectsParameterListsInsideHeaderEntryAndGroundStorage) {
    for (const std::uint32_t pointer : {0U, 4U, static_cast<std::uint32_t>(kParameterEntry),
        static_cast<std::uint32_t>(kGrndAddress + 0xe0 - 8)}) {
        if (pointer == 0) continue;
        auto source = encounterSource(Endian::Little);
        writeU32(source, kParameterEntry + 0x10, pointer);
        if (pointer >= kGrndAddress) { writeU32(source, pointer, 1); writeU32(source, pointer + 4, 7); }
        const auto file = spice::mld::parsing::MldParser{}.parseBytes(source);
        auto request = encounterRequest(file);
        auto edit = parameterEdit();
        if (file.entries[1].entry.functionParameters->valid && !file.entries[1].entry.functionParameters->values.empty()) {
            edit.expectedParameterCount = *file.entries[1].entry.functionParameters->declaredCount;
            edit.expectedValue = file.entries[1].entry.functionParameters->values[0];
        }
        request.parameterEdits.push_back(edit);
        expectRejected(file, request);
    }
}

TEST(MldEncounterPatching, AcceptsTheExistingImportReceiptFingerprint) {
    for (int encoding = 0; encoding < 3; ++encoding) {
        const auto file = encounterFile(encoding == 0 ? Endian::Little : Endian::Big, encoding == 2);
        const auto imported = spice::mld::MldDocumentImporter::importBytes(file.sourceBytes);
        std::string importDiagnostics;
        for (const auto& d : imported.diagnostics) importDiagnostics += d.message + "\n";
        ASSERT_TRUE(imported.ok()) << importDiagnostics;
        auto request = encounterRequest(file);
        EXPECT_EQ(request.sourceSha256, imported.receipt.sourceSha256);
        EXPECT_EQ(request.sourceSize, imported.receipt.sourceSize);
        request.sourceSha256 = imported.receipt.sourceSha256;
        request.sourceSize = imported.receipt.sourceSize;
        request.parameterEdits.push_back(parameterEdit());
        const auto plan = planEncounterPatches(file, request);
        ASSERT_TRUE(plan.ok()) << encounterDiagnostics(plan);
        EXPECT_TRUE(materializeEncounterPatchPlan(file.sourceBytes, plan).ok());
    }
}


TEST(MldEncounterPatching, RejectsTruncatedCountsValuesAndEnclosingLists) {
    for (int fault = 0; fault < 4; ++fault) {
        auto source = encounterSource(Endian::Little);
        switch (fault) {
        case 0: writeU32(source, kParameterEntry + 0x10, static_cast<std::uint32_t>(source.size() - 2)); break;
        case 1: writeU32(source, kParameterPointer, 65537); break;
        case 2: writeU32(source, kParameterPointer, static_cast<std::uint32_t>(source.size() / 4)); break;
        case 3:
            writeU32(source, 0x20 + 0x0c, 0x1f0);
            writeU32(source, 0x1f0, 10);
            break;
        }
        const auto file = spice::mld::parsing::MldParser{}.parseBytes(source);
        auto request = encounterRequest(file); request.parameterEdits.push_back(parameterEdit());
        expectRejected(file, request);
    }
}

TEST(MldEncounterPatching, ValidatedPlanOwnsItsWritesAndCanBeReused) {
    auto file = encounterFile();
    const auto source = file.sourceBytes;
    auto request = encounterRequest(file); request.parameterEdits.push_back(parameterEdit());
    const auto plan = planEncounterPatches(file, request);
    ASSERT_TRUE(plan.ok());
    auto expected = source;
    writeU32(expected, kParameterPointer + 4, 0x89abcdef);
    request.parameterEdits[0].replacementValue = 0;
    request.sourceSha256.fill(0);
    file.sourceBytes.clear(); file.decodedBytes.clear(); file.entries.clear();
    EXPECT_EQ(materializeEncounterPatchPlan(source, plan).bytes, expected);
    EXPECT_EQ(materializeEncounterPatchPlan(source, plan).bytes, expected);
}

TEST(MldEncounterPatching, MixedWidthWriterRejectsPartialOverlapAndLateMismatchAtomically) {
    using spice::mld::patching::detail::ByteWrite;
    const std::vector<std::uint8_t> original(12, 0);
    for (const std::size_t offset : {std::size_t{3}, std::size_t{7}}) {
        auto bytes = original;
        std::vector<ByteWrite> writes{{2, {0, 0, 0, 0}, {1, 2, 3, 4}, "parameter[0]"},
            {offset, {0, 1}, {5, 6}, "triangle[0]"}};
        const auto result = spice::mld::patching::detail::applyWrites(bytes, writes);
        EXPECT_FALSE(result.ok()); EXPECT_EQ(result.appliedPatchCount, 0);
        EXPECT_EQ(bytes, original);
    }
}


TEST(MldEncounterPatching, LargeBatchUsesOneGroundResourceAndOneParameterList) {
    // Time only planning: the source parse and independent output reparse are test work.
    // Two batch sizes expose per-triangle whole-resource scans without a flaky timing assertion.
    for (const auto endian : {Endian::Little, Endian::Big}) for (const std::size_t count : {2048U, 8192U}) {
        constexpr std::size_t parameterCount = 4096;
        constexpr std::size_t listPointer = 0x120;
        constexpr std::size_t address = (listPointer + 4 + parameterCount * 4 + 15) & ~std::size_t{15};
        const auto ground = makeDreamcastGrnd(endian, count);
        std::vector<std::uint8_t> decoded(address + ground.size(), 0);
        std::copy(ground.begin(), ground.end(), decoded.begin() + address);
        writeU32(decoded, 0, 2, endian); writeU32(decoded, 4, 0x20, endian);
        writeU32(decoded, 8, listPointer, endian); writeU32(decoded, 12, address, endian);
        writeU32(decoded, 16, static_cast<std::uint32_t>(decoded.size()), endian);
        writeU32(decoded, 0x20 + 0x18, 0x100, endian);
        writeU32(decoded, 0x100, 1, endian); writeU32(decoded, 0x104, address, endian);
        writeU32(decoded, kParameterEntry, 77, endian); writeU32(decoded, kParameterEntry + 4, 123, endian);
        writeU32(decoded, kParameterEntry + 0x10, listPointer, endian);
        const std::string groundName = "ground", functionName = "testControl";
        std::copy(groundName.begin(), groundName.end(), decoded.begin() + 0x20 + 0x24);
        std::copy(functionName.begin(), functionName.end(), decoded.begin() + kParameterEntry + 0x24);
        writeU32(decoded, listPointer, parameterCount, endian);
        for (std::size_t i = 0; i < parameterCount; ++i) writeU32(decoded, listPointer + 4 + 4 * i, static_cast<std::uint32_t>(i), endian);
        const bool compressed = endian == Endian::Big;
        const auto source = compressed ? spice::compression::aklz::compress(decoded).bytes : decoded;
        const auto file = spice::mld::parsing::MldParser{}.parseBytes(source);
        ASSERT_EQ(file.groundResources.size(), 1);
        ASSERT_EQ(file.groundResources.at(address).grnd->mesh.triangleMetadata.size(), count);
        auto request = encounterRequest(file);
        auto expected = decoded;
        for (std::size_t i = 0; i < count; ++i) {
            request.triangleEdits.push_back({TriangleResourceKind::Grnd, address, {}, i, 7});
            writeU16(expected, address + kGrndStreamOffset + 12 * i + 10, 0x8046, endian);
        }
        for (std::size_t i = 0; i < parameterCount; ++i) {
            auto edit = parameterEdit(); edit.expectedParameterCount = parameterCount;
            edit.parameterIndex = i; edit.expectedValue = static_cast<std::uint32_t>(i);
            edit.replacementValue = edit.expectedValue | 0x80000000U;
            request.parameterEdits.push_back(edit);
            writeU32(expected, listPointer + 4 + 4 * i, edit.replacementValue, endian);
        }
        const auto begin = std::chrono::steady_clock::now();
        const auto plan = planEncounterPatches(file, request);
        const auto elapsed = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - begin).count();
        std::cout << "Encounter batch triangles=" << count << " parameters=" << parameterCount
            << " compressed=" << compressed << " planningMs=" << elapsed << '\n';
        ASSERT_TRUE(plan.ok()) << encounterDiagnostics(plan);
        const auto result = materializeEncounterPatchPlan(source, plan);
        ASSERT_TRUE(result.ok()); EXPECT_EQ(result.appliedPatchCount, count + parameterCount);
        const auto actual = compressed ? spice::compression::aklz::decompress(result.bytes).bytes : result.bytes;
        EXPECT_EQ(actual, expected);
        const auto reparsed = spice::mld::parsing::MldParser{}.parseBytes(result.bytes);
        ASSERT_EQ(reparsed.groundResources.at(address).grnd->mesh.triangleMetadata.size(), count);
        for (const auto& metadata : reparsed.groundResources.at(address).grnd->mesh.triangleMetadata)
            EXPECT_EQ(metadata.rawU16[2], 0x8046);
        EXPECT_EQ(reparsed.entries[1].entry.functionParameters->values.size(), parameterCount);
        EXPECT_EQ(reparsed.entries[1].entry.functionParameters->values.back(), 0x80000000U | (parameterCount - 1));
    }
}
