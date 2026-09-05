#pragma once

#include "MldDocumentSession.h"
#include "../../SpiceMLD/MldDocumentImporter.h"
#include "../../SpiceMLD/Model/MldFile.h"

#include <filesystem>
#include <span>

namespace spice::mix::documents {

[[nodiscard]] MldOverviewSnapshot projectMldOverview(const spice::mld::MldDocument& document,
    const spice::mld::MldImportReceipt& receipt, bool dirty = false);
[[nodiscard]] std::vector<MldEntrySnapshot> projectMldEntries(const spice::mld::MldDocument& document);
[[nodiscard]] std::vector<MldEntryDetailSnapshot> projectMldEntryDetails(const spice::mld::MldDocument& document);
[[nodiscard]] std::vector<MldTextureSnapshot> projectMldTextures(const spice::mld::MldDocument& document,
    const std::vector<bool>& dirtyTextures = {});
[[nodiscard]] std::vector<DocumentDiagnostic> projectMldDiagnostics(
    const std::vector<spice::mld::MldDocumentDiagnostic>& diagnostics);
[[nodiscard]] std::optional<RgbaImageSnapshot> projectMldTexturePreview(
    const spice::mld::MldDocument& document, std::size_t index);

[[nodiscard]] MldOverviewSnapshot projectMldOverview(const spice::mld::model::MldFile& file,
    const std::filesystem::path& sourcePath = {}, bool dirty = false);
[[nodiscard]] std::vector<MldEntrySnapshot> projectMldEntries(const spice::mld::model::MldFile& file);
[[nodiscard]] std::vector<MldEntryDetailSnapshot> projectMldEntryDetails(
    const spice::mld::model::MldFile& file);
[[nodiscard]] std::vector<MldTextureSnapshot> projectMldTextures(const spice::mld::model::MldFile& file,
    const std::vector<bool>& dirtyTextures = {});
[[nodiscard]] std::vector<DocumentDiagnostic> projectMldDiagnostics(const spice::mld::model::MldFile& file);
[[nodiscard]] std::optional<RgbaImageSnapshot> projectMldTexturePreview(
    const spice::mld::model::MldFile& file, std::size_t index);

} // namespace spice::mix::documents
