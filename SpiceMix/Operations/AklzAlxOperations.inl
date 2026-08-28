// Internal AKLZ and ALX operation helpers. Included by OperationExecution.cpp.

void ensureOutputParentDirectory(const std::filesystem::path& path) {
    const auto parent = path.parent_path();
    if (!parent.empty()) {
        std::filesystem::create_directories(parent);
    }
}

void runAklzUtility(
    const std::filesystem::path& inputPath,
    const std::filesystem::path& outputPath,
    const bool decompress) {
    if (!std::filesystem::exists(inputPath)) {
        throw std::runtime_error("AKLZ input file does not exist: " + inputPath.string());
    }
    if (!std::filesystem::is_regular_file(inputPath)) {
        throw std::runtime_error("AKLZ input path is not a file: " + inputPath.string());
    }

    const auto sourceBytes = readAllBytes(inputPath);
    std::vector<std::uint8_t> outputBytes{};
    if (decompress) {
        if (!spice::compression::aklz::isAklz(sourceBytes)) {
            throw std::runtime_error("Input is not AKLZ-compressed: " + inputPath.string());
        }
        const auto decoded = spice::compression::aklz::decompress(sourceBytes);
        if (!decoded.ok()) {
            throw std::runtime_error(
                "AKLZ decompression failed: " +
                std::string(spice::compression::aklz::errorToString(decoded.error)));
        }
        outputBytes = decoded.bytes;
    } else {
        const auto encoded = spice::compression::aklz::compress(sourceBytes);
        if (!encoded.ok()) {
            throw std::runtime_error(
                "AKLZ compression failed: " +
                std::string(spice::compression::aklz::errorToString(encoded.error)));
        }
        outputBytes = encoded.bytes;
    }

    ensureOutputParentDirectory(outputPath);
    if (!writeAllBytes(outputPath, std::span<const std::uint8_t>(outputBytes.data(), outputBytes.size()))) {
        throw std::runtime_error("Failed to write AKLZ utility output: " + outputPath.string());
    }
}
