#pragma once

#include "DocumentTypes.h"

#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace spice::mix {

struct MldOverviewSnapshot {
    std::filesystem::path sourcePath{};
    std::string platform{};
    std::string endian{};
    std::string parseStatus{};
    bool sourceWasAklz = false;
    std::size_t entryCount = 0;
    std::size_t textureCount = 0;
    std::size_t objectResourceCount = 0;
    std::size_t groundResourceCount = 0;
    std::size_t motionResourceCount = 0;
    bool dirty = false;
};

struct MldEntrySnapshot {
    std::size_t tableIndex = 0;
    std::uint32_t entryId = 0;
    std::int32_t tableId = 0;
    std::string functionName{};
    float positionX = 0;
    float positionY = 0;
    float positionZ = 0;
    float rotationX = 0;
    float rotationY = 0;
    float rotationZ = 0;
    float scaleX = 1;
    float scaleY = 1;
    float scaleZ = 1;
    std::size_t objectCount = 0;
    std::size_t groundCount = 0;
    std::size_t motionCount = 0;
    std::uint32_t texturesPointer = 0;
};

struct MldTextureSnapshot {
    std::size_t index = 0;
    std::string name{};
    TextureEncodingKind encoding = TextureEncodingKind::Unknown;
    std::string format{};
    std::string paletteFormat{};
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    bool mipmaps = false;
    bool hasGlobalIndex = false;
    std::uint32_t globalIndex = 0;
    std::size_t encodedSize = 0;
    bool decoded = false;
    bool dirty = false;
    std::vector<std::string> diagnostics{};
};

class MldDocumentSession {
public:
    struct OpenResult {
        std::shared_ptr<MldDocumentSession> session{};
        DocumentResult result{};
    };

    static OpenResult open(const std::filesystem::path& path, const DocumentContext& context = {});

    ~MldDocumentSession();
    MldDocumentSession(MldDocumentSession&&) noexcept;
    MldDocumentSession& operator=(MldDocumentSession&&) noexcept;
    MldDocumentSession(const MldDocumentSession&) = delete;
    MldDocumentSession& operator=(const MldDocumentSession&) = delete;

    [[nodiscard]] MldOverviewSnapshot overview() const;
    [[nodiscard]] std::vector<MldEntrySnapshot> entries() const;
    [[nodiscard]] std::vector<MldTextureSnapshot> textures() const;
    [[nodiscard]] std::vector<DocumentDiagnostic> diagnostics() const;
    [[nodiscard]] std::optional<RgbaImageSnapshot> texturePreview(std::size_t index) const;
    [[nodiscard]] bool dirty() const noexcept;

    DocumentResult replaceGvrTexture(std::size_t index,
        const std::filesystem::path& pngPath,
        const GvrEncodingOverrides& overrides = {},
        bool allowDimensionChange = false,
        const DocumentContext& context = {});
    DocumentResult revertTexture(std::size_t index);
    DocumentResult revertAll();
    DocumentResult extractNativeTexture(std::size_t index, const std::filesystem::path& outputPath,
        const DocumentContext& context = {}) const;
    DocumentResult exportTexturePng(std::size_t index, const std::filesystem::path& outputPath,
        const DocumentContext& context = {}) const;
    DocumentResult saveAs(const std::filesystem::path& outputPath, const DocumentContext& context = {});

private:
    struct Impl;
    explicit MldDocumentSession(std::unique_ptr<Impl> impl);
    std::unique_ptr<Impl> impl_;
};

} // namespace spice::mix
