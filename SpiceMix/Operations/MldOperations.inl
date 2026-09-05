// Included by OperationExecution.cpp inside its internal implementation namespace.
// MLD texture, parsing, SA3D comparison, manifest, and report operations.

std::size_t selectMldTextureIndex(
    const spice::mix::MldTextureSelector& selector,
    const spice::mld::model::MldTextureArchive& archive) {
    if (const auto* index = std::get_if<spice::mix::TextureIndex>(&selector)) {
        if (index->value >= archive.entries.size()) {
            throw std::runtime_error("MLD texture index is out of range");
        }
        return index->value;
    }

    const auto& name = std::get<spice::mix::TextureName>(selector).value;
    std::optional<std::size_t> match{};
    for (std::size_t i = 0; i < archive.entries.size(); ++i) {
        if (archive.entries[i].textureName != name) {
            continue;
        }
        if (match.has_value()) {
            throw std::runtime_error("MLD texture name is ambiguous: " + name);
        }
        match = i;
    }
    if (!match.has_value()) {
        throw std::runtime_error("MLD texture name was not found: " + name);
    }
    return *match;
}

struct ExtractedMldTexture {
    spice::mld::model::MldFile parsed{};
    std::size_t textureIndex = 0;
    spice::mld::model::MldTextureEntry texture{};
};

ExtractedMldTexture extractMldTexture(
    const spice::mix::MldTextureSelector& selector,
    const std::filesystem::path& sourceMldPath) {
    const auto sourceBytes = readAllBytes(sourceMldPath);
    if (sourceBytes.empty()) {
        throw std::runtime_error("failed to read source MLD: " + sourceMldPath.string());
    }

    spice::mld::parsing::MldParser parser{};
    auto parsed = parser.parseFile(std::span<const std::uint8_t>(sourceBytes.data(), sourceBytes.size()));
    if (!parsed.textureArchive.has_value() || parsed.textureArchive->entries.empty()) {
        throw std::runtime_error("source MLD has no parsed texture archive");
    }

    const auto textureIndex = selectMldTextureIndex(selector, *parsed.textureArchive);
    const auto texture = parsed.textureArchive->entries[textureIndex];
    if (texture.encodedData.empty()) {
        throw std::runtime_error("selected source texture has no preserved GVR payload");
    }

    return ExtractedMldTexture{
        .parsed = std::move(parsed),
        .textureIndex = textureIndex,
        .texture = texture,
    };
}

const char* mldDiagnosticSeverityName(const spice::mld::model::MldDiagnostic::Severity severity) {
    switch (severity) {
    case spice::mld::model::MldDiagnostic::Severity::Info:
        return "info";
    case spice::mld::model::MldDiagnostic::Severity::Warning:
        return "warning";
    case spice::mld::model::MldDiagnostic::Severity::Error:
        return "error";
    }
    return "unknown";
}

void writeMldDiagnostic(std::ostream& out, const char* key,
    const spice::mld::model::MldDiagnostic& diagnostic) {
    out << key << "=[" << mldDiagnosticSeverityName(diagnostic.severity) << "] " << diagnostic.message;
    if (diagnostic.sourceOffset.has_value()) {
        out << " @0x" << std::hex << *diagnostic.sourceOffset << std::dec;
    }
    out << "\n";
}

void writeMldTextureExtractReport(
    const std::filesystem::path& reportPath,
    const std::filesystem::path& sourceMldPath,
    const std::filesystem::path& outputPath,
    const ExtractedMldTexture& extracted,
    const char* outputKind,
    const std::optional<spice::gvm::ir::GvrPngExportResult>& pngResult = std::nullopt,
    const std::optional<std::filesystem::path>& intermediateGvrPath = std::nullopt) {
    if (reportPath.has_parent_path()) {
        std::filesystem::create_directories(reportPath.parent_path());
    }
    std::ofstream reportOut(reportPath, std::ios::binary);
    const auto& texture = extracted.texture;
    reportOut << "sourceMld=" << sourceMldPath.string() << "\n";
    reportOut << "output" << outputKind << "=" << outputPath.string() << "\n";
    if (intermediateGvrPath.has_value()) {
        reportOut << "intermediateGvr=" << intermediateGvrPath->string() << "\n";
    }
    reportOut << "textureIndex=" << texture.archiveTextureIndex << "\n";
    reportOut << "textureName=" << texture.textureName << "\n";
    reportOut << "sourceTextureFormat=" << texture.sourceFormat << "\n";
    reportOut << "sourcePaletteFormat=" << texture.sourcePaletteFormat << "\n";
    reportOut << "sourceWidth=" << texture.width << "\n";
    reportOut << "sourceHeight=" << texture.height << "\n";
    reportOut << "sourceHasMipmaps=" << (texture.hasMipmaps ? "true" : "false") << "\n";
    reportOut << "sourceHasGlobalIndex=" << (texture.hasGlobalIndex ? "true" : "false") << "\n";
    reportOut << "sourceGlobalIndex=" << texture.globalIndex << "\n";
    reportOut << "archiveTextureIndex=" << texture.archiveTextureIndex << "\n";
    reportOut << "archiveOffset=" << texture.archiveOffset << "\n";
    reportOut << "encodedDataOffset=" << texture.encodedDataOffset << "\n";
    reportOut << "encodedDataSize=" << texture.encodedDataSize << "\n";
    reportOut << "sourceFileSize=" << extracted.parsed.originalBytes.size() << "\n";
    reportOut << "sourceWasAklz=" << (extracted.parsed.sourceWasCompressedAklz ? "true" : "false") << "\n";
    for (const auto& diagnostic : extracted.parsed.parseDiagnostics) {
        writeMldDiagnostic(reportOut, "mldDiagnostic", diagnostic);
    }
    for (const auto& diagnostic : texture.diagnostics) {
        writeMldDiagnostic(reportOut, "textureDiagnostic", diagnostic);
    }
    if (pngResult.has_value()) {
        const auto& textureMetadata = pngResult->texture;
        reportOut << "pngWidth=" << textureMetadata.width << "\n";
        reportOut << "pngHeight=" << textureMetadata.height << "\n";
        reportOut << "pngTextureFormat=" << spice::gvm::model::to_string(textureMetadata.textureFormat) << "\n";
        reportOut << "pngPaletteFormat=" << spice::gvm::model::to_string(textureMetadata.paletteFormat) << "\n";
        reportOut << "pngHasMipmaps=" << (textureMetadata.hasMipmaps ? "true" : "false") << "\n";
        reportOut << "pngHasGlobalIndex=" << (textureMetadata.hasGlobalIndex ? "true" : "false") << "\n";
        reportOut << "pngGlobalIndex=" << textureMetadata.globalIndex << "\n";
        reportOut << "gvrSourceWasAklz=" << (pngResult->sourceWasAklz ? "true" : "false") << "\n";
        for (const auto& diagnostic : pngResult->diagnostics) {
            reportOut << "gvrDiagnostic=" << diagnostic << "\n";
        }
    }
}

void writeGvrToPngReport(
    const std::filesystem::path& reportPath,
    const std::filesystem::path& sourceGvrPath,
    const std::filesystem::path& outputPngPath,
    const spice::gvm::ir::GvrPngExportResult& result) {
    if (reportPath.has_parent_path()) {
        std::filesystem::create_directories(reportPath.parent_path());
    }
    std::ofstream reportOut(reportPath, std::ios::binary);
    const auto& texture = result.texture;
    reportOut << "sourceGvr=" << sourceGvrPath.string() << "\n";
    reportOut << "outputPng=" << outputPngPath.string() << "\n";
    reportOut << "width=" << texture.width << "\n";
    reportOut << "height=" << texture.height << "\n";
    reportOut << "textureFormat=" << spice::gvm::model::to_string(texture.textureFormat) << "\n";
    reportOut << "paletteFormat=" << spice::gvm::model::to_string(texture.paletteFormat) << "\n";
    reportOut << "hasMipmaps=" << (texture.hasMipmaps ? "true" : "false") << "\n";
    reportOut << "hasGlobalIndex=" << (texture.hasGlobalIndex ? "true" : "false") << "\n";
    reportOut << "globalIndex=" << texture.globalIndex << "\n";
    reportOut << "sourceWasAklz=" << (result.sourceWasAklz ? "true" : "false") << "\n";
    reportOut << "sourceSize=" << texture.sourceSize << "\n";
    for (const auto& diagnostic : result.diagnostics) {
        reportOut << "diagnostic=" << diagnostic << "\n";
    }
}

bool shouldCompressMldOutput(
    const spice::gvm::ir::AklzPolicy policy,
    const bool sourceWasCompressed) {
    switch (policy) {
    case spice::gvm::ir::AklzPolicy::Preserve:
        return sourceWasCompressed;
    case spice::gvm::ir::AklzPolicy::Compressed:
        return true;
    case spice::gvm::ir::AklzPolicy::Raw:
        return false;
    default:
        return sourceWasCompressed;
    }
}

std::size_t rebuiltArchiveSize(
    const spice::mld::model::MldTextureArchive& archive,
    const std::size_t replacementIndex,
    const std::size_t replacementSize) {
    std::size_t size = archive.archivePrefixBytes.size();
    for (std::size_t i = 0; i < archive.entries.size(); ++i) {
        size += i == replacementIndex ? replacementSize : archive.entries[i].encodedDataSize;
    }
    return size;
}

void writeMldTextureReplacementReport(
    const std::filesystem::path& reportPath,
    const std::filesystem::path& sourceMldPath,
    const std::filesystem::path& pngPath,
    const std::filesystem::path& outputMldPath,
    const spice::mld::model::MldTextureArchive& archive,
    const spice::mld::model::MldTextureEntry& sourceTexture,
    const spice::gvm::encoding::EncodeOptions& encodeOptions,
    const spice::gvm::ir::AklzPolicy aklzPolicy,
    const std::size_t replacementSize,
    const std::size_t sourceFileSize,
    const std::size_t sourcePayloadSize,
    const std::size_t outputFileSize,
    const spice::mld::parsing::MldParser& parser,
    std::span<const std::uint8_t> outputBytes) {
    std::ofstream reportOut(reportPath, std::ios::binary);
    const auto originalArchiveSize = archive.archiveEndOffset - archive.archiveStartOffset;
    const auto newArchiveSize = rebuiltArchiveSize(archive, sourceTexture.archiveTextureIndex, replacementSize);
    reportOut << "sourceMld=" << sourceMldPath.string() << "\n";
    reportOut << "sourcePng=" << pngPath.string() << "\n";
    reportOut << "outputMld=" << outputMldPath.string() << "\n";
    reportOut << "textureIndex=" << sourceTexture.archiveTextureIndex << "\n";
    reportOut << "textureName=" << sourceTexture.textureName << "\n";
    reportOut << "sourceTextureFormat=" << sourceTexture.sourceFormat << "\n";
    reportOut << "sourcePaletteFormat=" << sourceTexture.sourcePaletteFormat << "\n";
    reportOut << "sourceHasMipmaps=" << (sourceTexture.hasMipmaps ? "true" : "false") << "\n";
    reportOut << "sourceHasGlobalIndex=" << (sourceTexture.hasGlobalIndex ? "true" : "false") << "\n";
    reportOut << "sourceGlobalIndex=" << sourceTexture.globalIndex << "\n";
    reportOut << "outputTextureFormat=" << spice::gvm::model::to_string(encodeOptions.textureFormat) << "\n";
    reportOut << "outputPaletteFormat=" << spice::gvm::model::to_string(encodeOptions.paletteFormat) << "\n";
    reportOut << "outputHasMipmaps=" << (encodeOptions.generateMipmaps ? "true" : "false") << "\n";
    reportOut << "outputHasGlobalIndex=" << (encodeOptions.hasGlobalIndex ? "true" : "false") << "\n";
    reportOut << "outputGlobalIndex=" << encodeOptions.globalIndex << "\n";
    reportOut << "originalEncodedTextureSize=" << sourceTexture.encodedDataSize << "\n";
    reportOut << "replacementGvrSize=" << replacementSize << "\n";
    reportOut << "originalArchiveSize=" << originalArchiveSize << "\n";
    reportOut << "replacementArchiveSize=" << newArchiveSize << "\n";
    reportOut << "archiveSizeDelta=" << (static_cast<long long>(newArchiveSize) - static_cast<long long>(originalArchiveSize)) << "\n";
    reportOut << "sourceFileSize=" << sourceFileSize << "\n";
    reportOut << "sourcePayloadSize=" << sourcePayloadSize << "\n";
    reportOut << "outputFileSize=" << outputFileSize << "\n";
    reportOut << "fileSizeDelta=" << (static_cast<long long>(outputFileSize) - static_cast<long long>(sourceFileSize)) << "\n";
    reportOut << "aklzPolicy=" << spice::gvm::ir::to_string(aklzPolicy) << "\n";
    reportOut << "hasPostArchiveSuffix=" << (archive.archiveEndOffset < sourcePayloadSize ? "true" : "false") << "\n";

    const auto reparsed = parser.parseFile(outputBytes);
    if (!reparsed.textureArchive.has_value()) {
        reportOut << "reparseTextureArchive=false\n";
    } else {
        reportOut << "reparseTextureArchive=true\n";
        reportOut << "reparseTextureCount=" << reparsed.textureArchive->entries.size() << "\n";
    }
    for (const auto& diagnostic : reparsed.parseDiagnostics) {
        writeMldDiagnostic(reportOut, "reparseDiagnostic", diagnostic);
    }
}

void replaceMldTextureFromPngFile(
    const spice::mix::ReplaceMldTextureRequest& request) {
    const auto& sourceMldPath = request.source;
    const auto& pngPath = request.replacement;
    const auto& outputPath = request.output;
    const auto sourceBytes = readAllBytes(sourceMldPath);
    if (sourceBytes.empty()) {
        throw std::runtime_error("failed to read source MLD: " + sourceMldPath.string());
    }

    spice::mld::parsing::MldParser parser{};
    auto parsed = parser.parseFile(std::span<const std::uint8_t>(sourceBytes.data(), sourceBytes.size()));
    if (!parsed.textureArchive.has_value() || parsed.textureArchive->entries.empty()) {
        throw std::runtime_error("source MLD has no parsed texture archive");
    }
    const auto textureIndex = selectMldTextureIndex(request.selector, *parsed.textureArchive);
    const auto& sourceTexture = parsed.textureArchive->entries[textureIndex];
    if (sourceTexture.encodedData.empty()) {
        throw std::runtime_error("selected source texture has no preserved GVR payload");
    }

    const auto sourceMetadata = spice::gvm::ir::readGvrSourceMetadata(
        std::span<const std::uint8_t>(sourceTexture.encodedData.data(), sourceTexture.encodedData.size()));
    auto encodeOptions = buildReplaceGvrEncodeOptions(request.encoding, sourceMetadata);
    const auto image = spice::gvm::image::readPngRgba8(pngPath);
    if (!request.allowDimensionChange
        && (image.width != sourceMetadata.texture.width || image.height != sourceMetadata.texture.height)) {
        throw std::runtime_error("replacement PNG dimensions do not match the source MLD texture; pass --allow-dimension-change to allow this");
    }
    auto replacementGvr = spice::gvm::encoding::encodeGvr(image, encodeOptions);

    spice::mld::exporting::MldExportOptions exportOptions{};
    exportOptions.platform = parsed.sourcePlatform == spice::mld::model::TargetPlatform::Unknown
        ? spice::mld::model::TargetPlatform::GameCube
        : parsed.sourcePlatform;
    const auto aklzPolicy = toRuntimeAklzPolicy(request.encoding.aklz);
    exportOptions.compressAklz = shouldCompressMldOutput(aklzPolicy, parsed.sourceWasCompressedAklz);
    exportOptions.textureReplacement = spice::mld::exporting::MldTextureReplacement{
        .textureIndex = textureIndex,
        .encodedData = replacementGvr,
        .allowPostArchiveShift = request.allowPostArchiveShift,
    };

    const auto exported = spice::mld::exporting::MldFileExporter{}.exportFile(parsed, exportOptions);
    if (outputPath.has_parent_path()) {
        std::filesystem::create_directories(outputPath.parent_path());
    }
    if (!writeAllBytes(outputPath, std::span<const std::uint8_t>(exported.data(), exported.size()))) {
        throw std::runtime_error("failed to write replacement MLD output: " + outputPath.string());
    }

    writeMldTextureReplacementReport(outputPath.parent_path() / (outputPath.stem().string() + ".mld_texture_replace.txt"),
        sourceMldPath,
        pngPath,
        outputPath,
        *parsed.textureArchive,
        sourceTexture,
        encodeOptions,
        aklzPolicy,
        replacementGvr.size(),
        sourceBytes.size(),
        parsed.originalBytes.size(),
        exported.size(),
        parser,
        std::span<const std::uint8_t>(exported.data(), exported.size()));
}

void extractMldTextureToGvrFile(
    const spice::mix::ExtractMldTextureGvrRequest& request) {
    const auto& sourceMldPath = request.input;
    const auto& outputPath = request.output;
    const auto extracted = extractMldTexture(request.selector, sourceMldPath);
    if (!writeAllBytesCreatingParents(outputPath,
        std::span<const std::uint8_t>(extracted.texture.encodedData.data(), extracted.texture.encodedData.size()))) {
        throw std::runtime_error("failed to write extracted GVR output: " + outputPath.string());
    }

    writeMldTextureExtractReport(
        outputPath.parent_path() / (outputPath.stem().string() + ".mld_texture_extract_gvr.txt"),
        sourceMldPath,
        outputPath,
        extracted,
        "Gvr");
}

void convertGvrToPngFile(
    const std::filesystem::path& sourceGvrPath,
    const std::filesystem::path& outputPath) {
    const auto sourceBytes = readAllBytes(sourceGvrPath);
    if (sourceBytes.empty()) {
        throw std::runtime_error("failed to read source GVR: " + sourceGvrPath.string());
    }

    const auto exported = spice::gvm::ir::exportGvrPng(
        std::span<const std::uint8_t>(sourceBytes.data(), sourceBytes.size()),
        outputPath);
    writeGvrToPngReport(
        outputPath.parent_path() / (outputPath.stem().string() + ".gvr_to_png.txt"),
        sourceGvrPath,
        outputPath,
        exported);
}

void extractMldTextureToPngFile(
    const spice::mix::ExtractMldTexturePngRequest& request) {
    const auto& sourceMldPath = request.input;
    const auto& outputPath = request.output;
    const auto extracted = extractMldTexture(request.selector, sourceMldPath);
    if (request.gvrOutput.has_value()) {
        if (!writeAllBytesCreatingParents(*request.gvrOutput,
            std::span<const std::uint8_t>(extracted.texture.encodedData.data(), extracted.texture.encodedData.size()))) {
            throw std::runtime_error("failed to write intermediate extracted GVR: " + request.gvrOutput->string());
        }
    }

    const auto exported = spice::gvm::ir::exportGvrPng(
        std::span<const std::uint8_t>(extracted.texture.encodedData.data(), extracted.texture.encodedData.size()),
        outputPath);
    writeMldTextureExtractReport(
        outputPath.parent_path() / (outputPath.stem().string() + ".mld_texture_extract_png.txt"),
        sourceMldPath,
        outputPath,
        extracted,
        "Png",
        exported,
        request.gvrOutput);
}

std::string gvrSidecarStem(const std::filesystem::path& path) {
    auto filename = path.filename().string();
    if (endsWithInsensitive(filename, ".gvr.json")) {
        filename.resize(filename.size() - std::string(".gvr.json").size());
        return filename;
    }
    return path.stem().string();
}

std::string toSpatialBlockKindLabel(const spice::mld::parsing::ExtractedMldSpatialBlock::Kind kind) {
    switch (kind) {
    case spice::mld::parsing::ExtractedMldSpatialBlock::Kind::Grnd:
        return "grnd";
    case spice::mld::parsing::ExtractedMldSpatialBlock::Kind::Gobj:
        return "gobj";
    case spice::mld::parsing::ExtractedMldSpatialBlock::Kind::UnknownGround:
        return "unknown_ground";
    case spice::mld::parsing::ExtractedMldSpatialBlock::Kind::UnknownObject:
        return "unknown_object";
    default:
        return "unknown";
    }
}

std::string spatialBlockExtension(const spice::mld::parsing::ExtractedMldSpatialBlock::Kind kind) {
    switch (kind) {
    case spice::mld::parsing::ExtractedMldSpatialBlock::Kind::Grnd:
        return ".grnd.bin";
    case spice::mld::parsing::ExtractedMldSpatialBlock::Kind::Gobj:
        return ".gobj.bin";
    case spice::mld::parsing::ExtractedMldSpatialBlock::Kind::UnknownGround:
        return ".ground.bin";
    case spice::mld::parsing::ExtractedMldSpatialBlock::Kind::UnknownObject:
        return ".object.bin";
    default:
        return ".bin";
    }
}

std::string hexU32ForFile(std::uint32_t value) {
    constexpr char digits[] = "0123456789abcdef";
    std::string result = "0x00000000";
    for (int i = 9; i >= 2; --i) {
        result[static_cast<std::size_t>(i)] = digits[value & 0xFu];
        value >>= 4;
    }
    return result;
}

void writeJsonU32Array(std::ostream& out, const std::vector<std::uint32_t>& values) {
    out << "[";
    for (std::size_t i = 0; i < values.size(); ++i) {
        if (i > 0) {
            out << ", ";
        }
        out << values[i];
    }
    out << "]";
}

void writeJsonStringArray(std::ostream& out, const std::vector<std::string>& values) {
    out << "[";
    for (std::size_t i = 0; i < values.size(); ++i) {
        if (i > 0) {
            out << ", ";
        }
        out << "\"" << jsonEscape(values[i]) << "\"";
    }
    out << "]";
}

void writeJsonU8Array(std::ostream& out, const std::vector<std::uint8_t>& values) {
    out << "[";
    for (std::size_t i = 0; i < values.size(); ++i) {
        if (i > 0) {
            out << ", ";
        }
        out << static_cast<unsigned int>(values[i]);
    }
    out << "]";
}

void writeSctDetailedJson(const std::filesystem::path& outPath, const spice::sct::SctParseResult& result) {
    std::ofstream out(outPath, std::ios::binary);
    out << "{\n";
    out << "  \"schema\": \"spice_sct_parse_v1\",\n";
    out << "  \"source\": \"" << jsonEscape(result.file.sourcePath) << "\",\n";
    out << "  \"parseOk\": " << (result.parseOk ? "true" : "false") << ",\n";
    out << "  \"sectionCount\": " << result.file.sections.size() << ",\n";
    out << "  \"sections\": [\n";
    for (std::size_t si = 0; si < result.file.sections.size(); ++si) {
        const auto& section = result.file.sections[si];
        out << "    {\n";
        out << "      \"index\": " << section.id.index << ",\n";
        out << "      \"name\": \"" << jsonEscape(section.id.name) << "\",\n";
        out << "      \"startOffset\": " << section.startOffset << ",\n";
        out << "      \"startOffsetHex\": \"" << hexU32ForFile(section.startOffset) << "\",\n";
        out << "      \"endOffset\": " << section.endOffset << ",\n";
        out << "      \"endOffsetHex\": \"" << hexU32ForFile(section.endOffset) << "\",\n";
        out << "      \"isStringSection\": " << (section.isStringSection ? "true" : "false") << ",\n";
        out << "      \"heuristics\": {\n";
        out << "        \"touchesFlags\": " << (section.heuristicEvidence.touchesFlags ? "true" : "false") << ",\n";
        out << "        \"branchesOnFlags\": " << (section.heuristicEvidence.branchesOnFlags ? "true" : "false") << ",\n";
        out << "        \"writesFlags\": " << (section.heuristicEvidence.writesFlags ? "true" : "false") << ",\n";
        out << "        \"hasSwitch\": " << (section.heuristicEvidence.hasSwitch ? "true" : "false") << ",\n";
        out << "        \"hasLongLinearSequence\": " << (section.heuristicEvidence.hasLongLinearSequence ? "true" : "false") << ",\n";
        out << "        \"hasPlayerReposition\": " << (section.heuristicEvidence.hasPlayerReposition ? "true" : "false") << ",\n";
        out << "        \"hasCameraOrTimingLikeOps\": " << (section.heuristicEvidence.hasCameraOrTimingLikeOps ? "true" : "false") << ",\n";
        out << "        \"likelyTrigger\": " << (section.heuristicEvidence.likelyTrigger ? "true" : "false") << ",\n";
        out << "        \"likelyCutscene\": " << (section.heuristicEvidence.likelyCutscene ? "true" : "false") << ",\n";
        out << "        \"notes\": ";
        writeJsonStringArray(out, section.heuristicEvidence.notes);
        out << "\n";
        out << "      },\n";
        out << "      \"instructions\": [\n";
        for (std::size_t ii = 0; ii < section.instructions.size(); ++ii) {
            const auto& inst = section.instructions[ii];
            out << "        {\n";
            out << "          \"offset\": " << inst.offset << ",\n";
            out << "          \"offsetHex\": \"" << hexU32ForFile(inst.offset) << "\",\n";
            out << "          \"opcode\": " << inst.opcode << ",\n";
            out << "          \"opcodeHex\": \"" << hexU32ForFile(inst.opcode) << "\",\n";
            out << "          \"decodeOk\": " << (inst.decodeOk ? "true" : "false") << ",\n";
            out << "          \"sizeBytes\": " << inst.sizeBytes << ",\n";
            out << "          \"operands\": ";
            writeJsonU32Array(out, inst.operands);
            out << ",\n";
            out << "          \"operandHex\": [";
            for (std::size_t oi = 0; oi < inst.operands.size(); ++oi) {
                if (oi > 0) {
                    out << ", ";
                }
                out << "\"" << hexU32ForFile(inst.operands[oi]) << "\"";
            }
            out << "],\n";
            out << "          \"scptAnalyzeOperandIndexes\": ";
            writeJsonU8Array(out, inst.scptAnalyzeOperandIndexes);
            out << ",\n";
            out << "          \"scptParameterValueRecords\": [\n";
            for (std::size_t ri = 0; ri < inst.scptParameterValueRecords.size(); ++ri) {
                const auto& record = inst.scptParameterValueRecords[ri];
                out << "            {\n";
                out << "              \"parameterIndex\": " << static_cast<unsigned int>(record.parameterIndex) << ",\n";
                out << "              \"operandStartWordIndex\": " << record.operandStartWordIndex << ",\n";
                out << "              \"operandWordCount\": " << record.operandWordCount << ",\n";
                out << "              \"hitStopCode\": " << (record.hitStopCode ? "true" : "false") << ",\n";
                out << "              \"resolvedValue\": \"" << jsonEscape(record.resolvedValue) << "\",\n";
                out << "              \"evaluationTrace\": [\n";
                for (std::size_t ti = 0; ti < record.evaluationTrace.size(); ++ti) {
                    const auto& trace = record.evaluationTrace[ti];
                    out << "                {"
                        << "\"rawWord\": " << trace.rawWord
                        << ", \"rawWordHex\": \"" << hexU32ForFile(trace.rawWord) << "\""
                        << ", \"interpretedValue\": \"" << jsonEscape(trace.interpretedValue) << "\""
                        << "}";
                    if (ti + 1 < record.evaluationTrace.size()) {
                        out << ",";
                    }
                    out << "\n";
                }
                out << "              ]\n";
                out << "            }";
                if (ri + 1 < inst.scptParameterValueRecords.size()) {
                    out << ",";
                }
                out << "\n";
            }
            out << "          ]\n";
            out << "        }";
            if (ii + 1 < section.instructions.size()) {
                out << ",";
            }
            out << "\n";
        }
        out << "      ],\n";
        out << "      \"blocks\": [\n";
        for (std::size_t bi = 0; bi < section.blocks.size(); ++bi) {
            const auto& block = section.blocks[bi];
            out << "        {\n";
            out << "          \"startOffset\": " << block.startOffset << ",\n";
            out << "          \"startOffsetHex\": \"" << hexU32ForFile(block.startOffset) << "\",\n";
            out << "          \"endOffset\": " << block.endOffset << ",\n";
            out << "          \"endOffsetHex\": \"" << hexU32ForFile(block.endOffset) << "\",\n";
            out << "          \"instructionOffsets\": ";
            writeJsonU32Array(out, block.instructionOffsets);
            out << ",\n";
            out << "          \"successorOffsets\": ";
            writeJsonU32Array(out, block.successorOffsets);
            out << "\n";
            out << "        }";
            if (bi + 1 < section.blocks.size()) {
                out << ",";
            }
            out << "\n";
        }
        out << "      ],\n";
        out << "      \"unknownRegions\": [\n";
        for (std::size_t ui = 0; ui < section.unknownRegions.size(); ++ui) {
            const auto& region = section.unknownRegions[ui];
            out << "        {\n";
            out << "          \"startOffset\": " << region.startOffset << ",\n";
            out << "          \"startOffsetHex\": \"" << hexU32ForFile(region.startOffset) << "\",\n";
            out << "          \"endOffset\": " << region.endOffset << ",\n";
            out << "          \"endOffsetHex\": \"" << hexU32ForFile(region.endOffset) << "\",\n";
            out << "          \"sizeBytes\": " << region.rawBytes.size() << ",\n";
            out << "          \"reason\": \"" << jsonEscape(region.reason) << "\"\n";
            out << "        }";
            if (ui + 1 < section.unknownRegions.size()) {
                out << ",";
            }
            out << "\n";
        }
        out << "      ]\n";
        out << "    }";
        if (si + 1 < result.file.sections.size()) {
            out << ",";
        }
        out << "\n";
    }
    out << "  ],\n";
    out << "  \"diagnostics\": [\n";
    for (std::size_t di = 0; di < result.diagnostics.size(); ++di) {
        const auto& diagnostic = result.diagnostics[di];
        out << "    {"
            << "\"section\": \"" << jsonEscape(diagnostic.section) << "\""
            << ", \"offset\": " << diagnostic.offset
            << ", \"offsetHex\": \"" << hexU32ForFile(diagnostic.offset) << "\""
            << ", \"message\": \"" << jsonEscape(diagnostic.message) << "\""
            << "}";
        if (di + 1 < result.diagnostics.size()) {
            out << ",";
        }
        out << "\n";
    }
    out << "  ]\n";
    out << "}\n";
}

void writeSpatialBlockManifest(
    const std::filesystem::path& outPath,
    const std::string_view fixtureId,
    const std::vector<std::filesystem::path>& blockPaths,
    const std::vector<spice::mld::parsing::ExtractedMldSpatialBlock>& blocks) {
    std::ofstream out(outPath, std::ios::binary);
    out << "{\n";
    out << "  \"schema\": \"spice_mld_spatial_block_manifest_v1\",\n";
    out << "  \"fixture_id\": \"" << jsonEscape(std::string(fixtureId)) << "\",\n";
    out << "  \"blocks\": [\n";
    for (std::size_t i = 0; i < blocks.size(); ++i) {
        const auto& block = blocks[i];
        out << "    {\n";
        out << "      \"index\": " << i << ",\n";
        out << "      \"kind\": \"" << toSpatialBlockKindLabel(block.kind) << "\",\n";
        out << "      \"tag\": \"" << jsonEscape(block.tag) << "\",\n";
        out << "      \"offset\": " << block.offset << ",\n";
        out << "      \"offset_hex\": \"" << hexU32ForFile(block.offset) << "\",\n";
        out << "      \"size\": " << block.size << ",\n";
        out << "      \"size_source\": \"" << jsonEscape(block.sizeSource) << "\",\n";
        out << "      \"path\": \"" << jsonEscape(i < blockPaths.size() ? std::filesystem::absolute(blockPaths[i]).string() : std::string{}) << "\",\n";
        out << "      \"owners\": [\n";
        for (std::size_t oi = 0; oi < block.owners.size(); ++oi) {
            const auto& owner = block.owners[oi];
            out << "        {"
                << "\"entry_id\": " << owner.sourceEntryId
                << ", \"table_index\": " << owner.tableIndex
                << ", \"fxn\": \"" << jsonEscape(owner.fxnName) << "\""
                << ", \"role\": \"" << jsonEscape(owner.role) << "\""
                << "}";
            if (oi + 1 < block.owners.size()) {
                out << ",";
            }
            out << "\n";
        }
        out << "      ],\n";
        out << "      \"header_probe\": [\n";
        for (std::size_t pi = 0; pi < block.headerProbe.size(); ++pi) {
            const auto& item = block.headerProbe[pi];
            out << "        {"
                << "\"key\": \"" << jsonEscape(item.first) << "\""
                << ", \"value\": \"" << jsonEscape(item.second) << "\""
                << "}";
            if (pi + 1 < block.headerProbe.size()) {
                out << ",";
            }
            out << "\n";
        }
        out << "      ]\n";
        out << "    }";
        if (i + 1 < blocks.size()) {
            out << ",";
        }
        out << "\n";
    }
    out << "  ]\n";
    out << "}\n";
}

void writeExtractedSpatialBlocks(
    spice::mix::OperationContext& context,
    const std::filesystem::path& outputDir,
    const std::string_view fixtureId,
    const std::vector<spice::mld::parsing::ExtractedMldSpatialBlock>& blocks) {
    const auto blockDir = outputDir / (std::string(fixtureId) + ".mld_blocks");
    std::filesystem::create_directories(blockDir);

    std::vector<std::filesystem::path> blockPaths{};
    blockPaths.reserve(blocks.size());
    for (std::size_t i = 0; i < blocks.size(); ++i) {
        const auto& block = blocks[i];
        const auto fileName = std::string(fixtureId) +
            "." + toSpatialBlockKindLabel(block.kind) +
            "_" + std::to_string(i) +
            "_" + hexU32ForFile(block.offset) +
            spatialBlockExtension(block.kind);
        const auto blockPath = blockDir / fileName;
        if (!writeAllBytes(blockPath, std::span<const std::uint8_t>(block.bytes.data(), block.bytes.size()))) {
            emit(context, spice::mix::EventLevel::Warning,
                "WARNING: failed to write extracted GRND/GOBJ block: ", blockPath.string());
            blockPaths.push_back({});
            continue;
        }
        blockPaths.push_back(blockPath);
    }

    writeSpatialBlockManifest(blockDir / "manifest.json", fixtureId, blockPaths, blocks);
}
