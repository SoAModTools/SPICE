// Included by OperationExecution.cpp inside its internal implementation namespace.
// SCT and joined SST/SML parsing and research-export support.

bool canReadSpan(std::span<const std::uint8_t> bytes, std::size_t offset, std::size_t size) {
    return offset <= bytes.size() && size <= bytes.size() - offset;
}

std::optional<std::uint32_t> readBeU32Span(std::span<const std::uint8_t> bytes, std::size_t offset) {
    if (!canReadSpan(bytes, offset, 4U)) {
        return std::nullopt;
    }
    return (static_cast<std::uint32_t>(bytes[offset]) << 24U) |
        (static_cast<std::uint32_t>(bytes[offset + 1U]) << 16U) |
        (static_cast<std::uint32_t>(bytes[offset + 2U]) << 8U) |
        static_cast<std::uint32_t>(bytes[offset + 3U]);
}

std::optional<std::int32_t> readBeI32Span(std::span<const std::uint8_t> bytes, std::size_t offset) {
    const auto raw = readBeU32Span(bytes, offset);
    if (!raw.has_value()) {
        return std::nullopt;
    }
    return static_cast<std::int32_t>(*raw);
}

std::optional<float> readBeF32Span(std::span<const std::uint8_t> bytes, std::size_t offset) {
    const auto raw = readBeU32Span(bytes, offset);
    if (!raw.has_value()) {
        return std::nullopt;
    }
    return std::bit_cast<float>(*raw);
}

std::optional<spice::sstsml::exporting::SmlBlenderIrSstPlacementOverlay> sstType0PlacementOverlayForRecord(
    const std::optional<spice::sstsml::SstParseResult>& sstParsed,
    std::size_t recordIndex) {
    if (!sstParsed.has_value()) {
        return std::nullopt;
    }

    const auto blockIt = std::find_if(sstParsed->commandBlocks.begin(), sstParsed->commandBlocks.end(), [&](const auto& block) {
        return block.topLevelRecordIndex == recordIndex;
    });
    if (blockIt == sstParsed->commandBlocks.end()) {
        return std::nullopt;
    }

    const auto commandIt = std::find_if(blockIt->commands.begin(), blockIt->commands.end(), [](const auto& command) {
        return command.type == 0 && command.payloadInBounds;
    });
    if (commandIt == blockIt->commands.end()) {
        return std::nullopt;
    }

    const auto payload = std::span<const std::uint8_t>(commandIt->payloadBytes.data(), commandIt->payloadBytes.size());
    spice::sstsml::exporting::SmlBlenderIrSstPlacementOverlay overlay{};

    const auto px = readBeF32Span(payload, 0x1CU);
    const auto py = readBeF32Span(payload, 0x20U);
    const auto pz = readBeF32Span(payload, 0x24U);
    if (px.has_value() && py.has_value() && pz.has_value()) {
        overlay.hasPosition = true;
        overlay.position = spice::mld::model::Vec3{ *px, *py, *pz };
    }

    const auto sx = readBeF32Span(payload, 0x34U);
    const auto sy = readBeF32Span(payload, 0x38U);
    const auto sz = readBeF32Span(payload, 0x3CU);
    if (sx.has_value() && sy.has_value() && sz.has_value()) {
        overlay.hasScale = true;
        overlay.scale = spice::mld::model::Vec3{ *sx, *sy, *sz };
    }

    const auto rx = readBeI32Span(payload, 0x28U);
    const auto ry = readBeI32Span(payload, 0x2CU);
    const auto rz = readBeI32Span(payload, 0x30U);
    if (rx.has_value() && ry.has_value() && rz.has_value()) {
        overlay.hasRotationRaw = true;
        overlay.rotationRaw = spice::mld::model::Vec3{
            static_cast<float>(*rx),
            static_cast<float>(*ry),
            static_cast<float>(*rz),
        };
    }

    std::ostringstream source;
    source << "SST record " << recordIndex
           << " command " << commandIt->index
           << " payloadOffset=0x" << std::hex << commandIt->payloadOffset;
    overlay.sourceDescription = source.str();
    return overlay;
}

std::string toLowerCopy(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return value;
}

bool sameVec3(const spice::mld::model::Vec3& a, const spice::mld::model::Vec3& b) {
    return a.x == b.x && a.y == b.y && a.z == b.z;
}

bool sameQuat(const spice::mld::model::Quat& a, const spice::mld::model::Quat& b) {
    return a.x == b.x && a.y == b.y && a.z == b.z && a.w == b.w;
}

bool vec3KeysVary(const std::vector<spice::mld::model::BlenderIrVec3Keyframe>& keys) {
    if (keys.size() < 2U) {
        return false;
    }

    const auto first = keys.front().value;
    return std::any_of(keys.begin() + 1, keys.end(), [&](const auto& key) {
        return !sameVec3(first, key.value);
    });
}

bool quatKeysVary(const std::vector<spice::mld::model::BlenderIrQuatKeyframe>& keys) {
    if (keys.size() < 2U) {
        return false;
    }

    const auto first = keys.front().value;
    return std::any_of(keys.begin() + 1, keys.end(), [&](const auto& key) {
        return !sameQuat(first, key.value);
    });
}

spice::sstsml::SmlBlenderIrEntrySummary summarizeSmlEntryBlenderIr(
    const spice::mld::model::BlenderIrScene& scene) {
    spice::sstsml::SmlBlenderIrEntrySummary summary{};
    summary.meshCount = scene.meshes.size();
    summary.objectTreeCount = scene.objectTrees.size();
    summary.indexEntryCount = scene.indexEntries.size();
    summary.textureCount = scene.textures.size();
    summary.animationCount = scene.animations.size();

    for (const auto& entry : scene.indexEntries) {
        if (!entry.fxnName.empty()) {
            summary.indexEntryNames.push_back(entry.fxnName);
        }
    }

    for (const auto& animation : scene.animations) {
        summary.animationNodeCount += animation.nodes.size();
        for (const auto& node : animation.nodes) {
            summary.animationPositionKeyCount += node.position.size();
            summary.animationRotationKeyCount += node.eulerRotation.size();
            summary.animationScaleKeyCount += node.scale.size();
            summary.animationQuaternionKeyCount += node.quaternionRotation.size();

            const auto appendVaryingChannel = [&](std::string channelName) {
                ++summary.varyingAnimationChannelCount;
                if (summary.varyingAnimationChannels.size() < 32U) {
                    summary.varyingAnimationChannels.push_back(
                        "table=" + std::to_string(animation.tableIndex) +
                        ",objectTree=" + std::to_string(animation.objectTreeIndex) +
                        ",motionSlot=" + std::to_string(animation.motionSlot) +
                        ",node=" + std::to_string(node.nodeIndex) +
                        ",channel=" + channelName);
                }
            };

            if (vec3KeysVary(node.position)) {
                appendVaryingChannel("position");
            }
            if (vec3KeysVary(node.eulerRotation)) {
                appendVaryingChannel("eulerRotation");
            }
            if (vec3KeysVary(node.scale)) {
                appendVaryingChannel("scale");
            }
            if (quatKeysVary(node.quaternionRotation)) {
                appendVaryingChannel("quaternionRotation");
            }
        }
    }

    return summary;
}

void writeSctReport(const std::filesystem::path& outPath, const spice::sct::SctParseResult& result) {
    std::ofstream out(outPath, std::ios::binary);
    out << "source=" << result.file.sourcePath << "\n";
    out << "parseOk=" << (result.parseOk ? "true" : "false") << "\n";
    out << "sections=" << result.file.sections.size() << "\n\n";

    for (const auto& section : result.file.sections) {
        out << "[section] index=" << section.id.index << " name=" << section.id.name << "\n";
        out << "  startOffset=" << section.startOffset << " endOffset=" << section.endOffset << "\n";
        out << "  instructions=" << section.instructions.size()
            << " blocks=" << section.blocks.size()
            << " unknownRegions=" << section.unknownRegions.size() << "\n";
        out << "  heuristics: trigger=" << section.heuristicEvidence.likelyTrigger
            << " cutscene=" << section.heuristicEvidence.likelyCutscene
            << " switch=" << section.heuristicEvidence.hasSwitch
            << " flagsTouched=" << section.heuristicEvidence.touchesFlags << "\n";
    }

    if (!result.diagnostics.empty()) {
        out << "\n[diagnostics]\n";
        for (const auto& diagnostic : result.diagnostics) {
            out << "- @" << diagnostic.offset << " " << diagnostic.message << "\n";
        }
    }
}

