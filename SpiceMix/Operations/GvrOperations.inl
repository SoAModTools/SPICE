// Included by OperationExecution.cpp inside its internal implementation namespace.
// Standalone and batch GVR encoding operations.

bool isSupportedGvrEncodeFormat(const spice::gvm::model::TextureFormat format) {
    switch (format) {
    case spice::gvm::model::TextureFormat::I4:
    case spice::gvm::model::TextureFormat::I8:
    case spice::gvm::model::TextureFormat::IA4:
    case spice::gvm::model::TextureFormat::IA8:
    case spice::gvm::model::TextureFormat::RGB565:
    case spice::gvm::model::TextureFormat::RGBA8:
    case spice::gvm::model::TextureFormat::RGB5A3:
    case spice::gvm::model::TextureFormat::CMPR:
    case spice::gvm::model::TextureFormat::CI4:
    case spice::gvm::model::TextureFormat::CI8:
    case spice::gvm::model::TextureFormat::CI14X2:
        return true;
    default:
        return false;
    }
}

bool isIndexedGvrFormat(const spice::gvm::model::TextureFormat format) {
    return format == spice::gvm::model::TextureFormat::CI4 ||
        format == spice::gvm::model::TextureFormat::CI8 ||
        format == spice::gvm::model::TextureFormat::CI14X2;
}

spice::gvm::encoding::EncodeOptions buildCreateGvrEncodeOptions(
    const spice::mix::GvrEncodingSettings& settings) {
    spice::gvm::encoding::EncodeOptions options{};
    options.textureFormat = settings.format.has_value()
        ? toRuntimeTextureFormat(*settings.format)
        : spice::gvm::model::TextureFormat::RGBA8;
    options.paletteFormat = isIndexedGvrFormat(options.textureFormat)
        ? (settings.paletteFormat.has_value()
            ? toRuntimePaletteFormat(*settings.paletteFormat)
            : spice::gvm::model::PaletteFormat::RGB5A3)
        : spice::gvm::model::PaletteFormat::None;
    options.generateMipmaps = settings.mipmaps.value_or(false);
    if (settings.globalIndex.kind == spice::mix::GvrGlobalIndexKind::Value) {
        options.hasGlobalIndex = true;
        options.globalIndex = settings.globalIndex.value;
    }
    return options;
}

spice::gvm::encoding::EncodeOptions buildReplaceGvrEncodeOptions(
    const spice::mix::GvrEncodingSettings& settings,
    const spice::gvm::ir::GvrSourceMetadata& sourceMetadata) {
    spice::gvm::encoding::EncodeOptions options{};
    options.textureFormat = settings.format.has_value()
        ? toRuntimeTextureFormat(*settings.format)
        : sourceMetadata.texture.textureFormat;
    if (!isSupportedGvrEncodeFormat(options.textureFormat)) {
        throw std::runtime_error("cannot preserve unsupported source GVR texture format: "
            + spice::gvm::model::to_string(sourceMetadata.texture.textureFormat));
    }
    options.paletteFormat = isIndexedGvrFormat(options.textureFormat)
        ? (settings.paletteFormat.has_value()
            ? toRuntimePaletteFormat(*settings.paletteFormat)
            :
            isIndexedGvrFormat(sourceMetadata.texture.textureFormat)
                ? sourceMetadata.texture.paletteFormat
                : spice::gvm::model::PaletteFormat::RGB5A3)
        : spice::gvm::model::PaletteFormat::None;
    options.generateMipmaps = settings.mipmaps.value_or(sourceMetadata.texture.hasMipmaps);
    switch (settings.globalIndex.kind) {
    case spice::mix::GvrGlobalIndexKind::Preserve:
        options.hasGlobalIndex = sourceMetadata.texture.hasGlobalIndex;
        options.globalIndex = sourceMetadata.texture.globalIndex;
        break;
    case spice::mix::GvrGlobalIndexKind::None:
        options.hasGlobalIndex = false;
        options.globalIndex = 0;
        break;
    case spice::mix::GvrGlobalIndexKind::Value:
        options.hasGlobalIndex = true;
        options.globalIndex = settings.globalIndex.value;
        break;
    default:
        break;
    }
    return options;
}

void writeGvrEncodeReport(
    const std::filesystem::path& reportPath,
    const std::filesystem::path& pngPath,
    const std::filesystem::path& outputPath,
    const spice::gvm::encoding::EncodeOptions& encodeOptions,
    const spice::gvm::ir::AklzPolicy aklzPolicy,
    const std::vector<std::string>& diagnostics) {
    std::ofstream reportOut(reportPath, std::ios::binary);
    reportOut << "sourcePng=" << pngPath.string() << "\n";
    reportOut << "output=" << outputPath.string() << "\n";
    reportOut << "textureFormat=" << spice::gvm::model::to_string(encodeOptions.textureFormat) << "\n";
    reportOut << "paletteFormat=" << spice::gvm::model::to_string(encodeOptions.paletteFormat) << "\n";
    reportOut << "hasMipmaps=" << (encodeOptions.generateMipmaps ? "true" : "false") << "\n";
    reportOut << "hasGlobalIndex=" << (encodeOptions.hasGlobalIndex ? "true" : "false") << "\n";
    reportOut << "globalIndex=" << encodeOptions.globalIndex << "\n";
    reportOut << "aklzPolicy=" << spice::gvm::ir::to_string(aklzPolicy) << "\n";
    for (const auto& diagnostic : diagnostics) {
        reportOut << "diagnostic=" << diagnostic << "\n";
    }
}

bool writeGvrOutput(const std::filesystem::path& outputPath, std::span<const std::uint8_t> bytes) {
    if (outputPath.has_parent_path()) {
        std::filesystem::create_directories(outputPath.parent_path());
    }
    return writeAllBytes(outputPath, bytes);
}

void createGvrFromPngFile(
    const spice::mix::GvrEncodingSettings& settings,
    const std::filesystem::path& pngPath,
    const std::filesystem::path& outputPath) {
    spice::gvm::ir::GvrPngEncodeOptions encodeRequest{};
    encodeRequest.encodeOptions = buildCreateGvrEncodeOptions(settings);
    encodeRequest.aklzPolicy = toRuntimeAklzPolicy(settings.aklz);
    encodeRequest.sourceWasAklz = false;
    const auto encoded = spice::gvm::ir::encodeGvrFromPng(pngPath, encodeRequest);
    if (!writeGvrOutput(outputPath, std::span<const std::uint8_t>(encoded.bytes.data(), encoded.bytes.size()))) {
        throw std::runtime_error("failed to write GVR output: " + outputPath.string());
    }
    writeGvrEncodeReport(outputPath.parent_path() / (outputPath.stem().string() + ".gvr.create.txt"),
        pngPath,
        outputPath,
        encodeRequest.encodeOptions,
        encodeRequest.aklzPolicy,
        encoded.diagnostics);
}

void replaceGvrFromPngFile(
    const spice::mix::GvrEncodingSettings& settings,
    const std::filesystem::path& sourceGvrPath,
    const std::filesystem::path& pngPath,
    const std::filesystem::path& outputPath) {
    const auto sourceBytes = readAllBytes(sourceGvrPath);
    if (sourceBytes.empty()) {
        throw std::runtime_error("failed to read source GVR: " + sourceGvrPath.string());
    }
    const auto sourceMetadata = spice::gvm::ir::readGvrSourceMetadata(
        std::span<const std::uint8_t>(sourceBytes.data(), sourceBytes.size()));
    spice::gvm::ir::GvrPngEncodeOptions encodeRequest{};
    encodeRequest.encodeOptions = buildReplaceGvrEncodeOptions(settings, sourceMetadata);
    encodeRequest.aklzPolicy = toRuntimeAklzPolicy(settings.aklz);
    encodeRequest.sourceWasAklz = sourceMetadata.sourceWasAklz;
    auto encoded = spice::gvm::ir::encodeGvrFromPng(pngPath, encodeRequest);
    encoded.diagnostics.insert(encoded.diagnostics.begin(), sourceMetadata.diagnostics.begin(), sourceMetadata.diagnostics.end());
    if (!writeGvrOutput(outputPath, std::span<const std::uint8_t>(encoded.bytes.data(), encoded.bytes.size()))) {
        throw std::runtime_error("failed to write GVR output: " + outputPath.string());
    }
    writeGvrEncodeReport(outputPath.parent_path() / (outputPath.stem().string() + ".gvr.replace.txt"),
        pngPath,
        outputPath,
        encodeRequest.encodeOptions,
        encodeRequest.aklzPolicy,
        encoded.diagnostics);
}

std::size_t createGvrBatch(
    const DirectoryOperation& operation,
    spice::mix::OperationContext& context) {
    std::size_t count = 0;
    for (const auto& entry : std::filesystem::directory_iterator(operation.paths.input)) {
        if (context.stopToken.stop_requested()) {
            break;
        }
        if (!entry.is_regular_file() || toLowerCopy(entry.path().extension().string()) != ".png") {
            continue;
        }
        emit(context, spice::mix::EventLevel::Progress,
            "  - Creating GVR: ", entry.path().filename().string());
        const auto outputPath = operation.paths.output / (entry.path().stem().string() + ".gvr");
        createGvrFromPngFile(operation.encoding, entry.path(), outputPath);
        ++count;
    }
    return count;
}

std::size_t replaceGvrBatch(
    const DirectoryOperation& operation,
    spice::mix::OperationContext& context) {
    std::size_t count = 0;
    for (const auto& entry : std::filesystem::directory_iterator(operation.paths.input)) {
        if (context.stopToken.stop_requested()) {
            break;
        }
        if (!entry.is_regular_file() || toLowerCopy(entry.path().extension().string()) != ".png") {
            continue;
        }
        emit(context, spice::mix::EventLevel::Progress,
            "  - Replacing GVR: ", entry.path().filename().string());
        const auto sourceGvrPath = operation.sourceGvrDirectory / (entry.path().stem().string() + ".gvr");
        if (!std::filesystem::exists(sourceGvrPath)) {
            throw std::runtime_error("missing replacement source GVR for PNG stem: " + entry.path().stem().string());
        }
        const auto outputPath = operation.paths.output / (entry.path().stem().string() + ".gvr");
        replaceGvrFromPngFile(operation.encoding, sourceGvrPath, entry.path(), outputPath);
        ++count;
    }
    return count;
}


