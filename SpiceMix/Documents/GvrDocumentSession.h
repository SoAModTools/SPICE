#pragma once

#include "DocumentTypes.h"

#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace spice::mix {

struct GvrDocumentSnapshot {
    std::optional<std::filesystem::path> sourcePath{};
    std::string displayName{};
    std::string format{};
    std::string paletteFormat{};
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    bool mipmaps = false;
    bool hasGlobalIndex = false;
    std::uint32_t globalIndex = 0;
    bool aklzWrapped = false;
    bool dirty = false;
    std::vector<std::string> diagnostics{};
};

class GvrDocumentSession {
public:
    struct OpenResult {
        std::shared_ptr<GvrDocumentSession> session{};
        DocumentResult result{};
    };

    static OpenResult open(const std::filesystem::path& path, const DocumentContext& context = {});
    static OpenResult createFromPng(const std::filesystem::path& path, const DocumentContext& context = {});

    ~GvrDocumentSession();
    GvrDocumentSession(GvrDocumentSession&&) noexcept;
    GvrDocumentSession& operator=(GvrDocumentSession&&) noexcept;
    GvrDocumentSession(const GvrDocumentSession&) = delete;
    GvrDocumentSession& operator=(const GvrDocumentSession&) = delete;

    [[nodiscard]] GvrDocumentSnapshot snapshot() const;
    [[nodiscard]] std::optional<RgbaImageSnapshot> preview() const;
    [[nodiscard]] bool dirty() const noexcept;

    DocumentResult replaceImage(const std::filesystem::path& pngPath,
        const GvrSaveOptions& options = {},
        bool allowDimensionChange = true,
        const DocumentContext& context = {});
    DocumentResult revert();
    DocumentResult exportPng(const std::filesystem::path& outputPath,
        const DocumentContext& context = {}) const;
    DocumentResult saveAs(const std::filesystem::path& outputPath, const DocumentContext& context = {});

private:
    struct Impl;
    explicit GvrDocumentSession(std::unique_ptr<Impl> impl);
    std::unique_ptr<Impl> impl_;
};

} // namespace spice::mix
