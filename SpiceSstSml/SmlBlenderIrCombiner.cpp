#include "SmlBlenderIrCombiner.h"

#include <algorithm>
#include <cctype>
#include <iterator>
#include <unordered_map>

namespace spice::sstsml::exporting {
namespace {

std::string makeSafeNameComponent(std::string value, std::string fallback) {
    for (char& c : value) {
        const auto uc = static_cast<unsigned char>(c);
        if (!std::isalnum(uc) && c != '_' && c != '-') {
            c = '_';
        }
    }

    const auto first = std::find_if(value.begin(), value.end(), [](char c) {
        return c != '_';
    });
    const auto last = std::find_if(value.rbegin(), value.rend(), [](char c) {
        return c != '_';
    }).base();
    if (first >= last) {
        return fallback;
    }
    return std::string(first, last);
}

} // namespace

std::string makeSmlEntryBlenderIrPrefix(const std::string& stem, std::size_t recordIndex) {
    const auto safeStem = makeSafeNameComponent(stem, "sml");
    return safeStem + "_entry_" + (recordIndex < 10U ? "0" : "") +
        std::to_string(recordIndex) + "__";
}

void namespaceSmlEntryBlenderIrScene(
    spice::mld::model::BlenderIrScene& scene,
    const std::string& stem,
    std::size_t recordIndex) {
    const auto prefix = makeSmlEntryBlenderIrPrefix(stem, recordIndex);
    std::unordered_map<std::string, std::string> renamedByOriginalName{};

    for (auto& texture : scene.textures) {
        const auto originalName = texture.textureName;
        const auto fallback = texture.hasTextureId
            ? "texture_" + std::to_string(texture.textureId)
            : "texture";
        const auto safeTextureName = makeSafeNameComponent(originalName, fallback);
        const auto namespacedName = prefix + safeTextureName;
        texture.textureName = namespacedName;
        if (!originalName.empty()) {
            renamedByOriginalName.emplace(originalName, namespacedName);
        }
        scene.diagnostics.push_back("SML export namespaced texture " + originalName + " -> " + namespacedName);
    }

    for (auto& mesh : scene.meshes) {
        for (auto& material : mesh.materials) {
            const auto it = renamedByOriginalName.find(material.textureName);
            if (it != renamedByOriginalName.end()) {
                material.textureName = it->second;
            } else if (!material.textureName.empty()) {
                material.textureName = prefix + makeSafeNameComponent(material.textureName, "texture");
            }
        }
    }

    for (auto& tree : scene.objectTrees) {
        tree.label = prefix + makeSafeNameComponent(tree.label, "object_tree");
    }
}

void SmlBlenderIrCombiner::appendEntryScene(
    spice::mld::model::BlenderIrScene entryScene,
    const std::string& stem,
    std::size_t recordIndex) {
    namespaceSmlEntryBlenderIrScene(entryScene, stem, recordIndex);

    const auto meshIndexBase = m_scene.meshes.size();
    const auto objectTreeIndexBase = m_scene.objectTrees.size();
    const auto indexEntryBase = m_scene.indexEntries.size();

    for (auto& tree : entryScene.objectTrees) {
        for (auto& node : tree.nodes) {
            if (node.meshIndex.has_value()) {
                *node.meshIndex += meshIndexBase;
            }
        }
    }

    for (auto& indexEntry : entryScene.indexEntries) {
        indexEntry.tableIndex += indexEntryBase;
        for (auto& meshIndex : indexEntry.meshIndices) {
            meshIndex += meshIndexBase;
        }
        for (auto& objectTreeIndex : indexEntry.objectTreeIndices) {
            objectTreeIndex += objectTreeIndexBase;
        }
        m_scene.indexEntries.push_back(std::move(indexEntry));
    }

    for (auto& animation : entryScene.animations) {
        animation.tableIndex += indexEntryBase;
        animation.objectTreeIndex += objectTreeIndexBase;
        m_scene.animations.push_back(std::move(animation));
    }

    m_scene.meshes.insert(
        m_scene.meshes.end(),
        std::make_move_iterator(entryScene.meshes.begin()),
        std::make_move_iterator(entryScene.meshes.end()));
    m_scene.objectTrees.insert(
        m_scene.objectTrees.end(),
        std::make_move_iterator(entryScene.objectTrees.begin()),
        std::make_move_iterator(entryScene.objectTrees.end()));
    m_scene.textures.insert(
        m_scene.textures.end(),
        std::make_move_iterator(entryScene.textures.begin()),
        std::make_move_iterator(entryScene.textures.end()));
    m_scene.diagnostics.push_back("SML combined Blender IR appended entry " + std::to_string(recordIndex) +
        " from prefix " + makeSmlEntryBlenderIrPrefix(stem, recordIndex));
    m_scene.diagnostics.insert(
        m_scene.diagnostics.end(),
        std::make_move_iterator(entryScene.diagnostics.begin()),
        std::make_move_iterator(entryScene.diagnostics.end()));
}

const spice::mld::model::BlenderIrScene& SmlBlenderIrCombiner::scene() const noexcept {
    return m_scene;
}

spice::mld::model::BlenderIrScene&& SmlBlenderIrCombiner::takeScene() noexcept {
    return std::move(m_scene);
}

} // namespace spice::sstsml::exporting

