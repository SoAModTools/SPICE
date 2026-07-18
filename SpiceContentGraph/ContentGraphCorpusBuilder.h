#pragma once

#include "ContentGraph.h"
#include "MldGraphBuilder.h"
#include "SctGraphBuilder.h"

#include <string>
#include <vector>

namespace spice::contentgraph {

struct ParsedSctGraphInput {
    std::string sourcePath{};
    spice::sct::SctParseResult parseResult{};
};

struct ParsedMldGraphInput {
    std::string sourcePath{};
    spice::mld::model::MldFile file{};
};

struct ContentGraphCorpusBuildOptions {
    SctGraphBuildOptions sctOptions{};
};

struct ContentGraphCorpusInput {
    std::vector<ParsedSctGraphInput> sctFiles{};
    std::vector<ParsedMldGraphInput> mldFiles{};
};

class ContentGraphCorpusBuilder {
public:
    [[nodiscard]] ContentGraph build(const ContentGraphCorpusInput& input,
        const ContentGraphCorpusBuildOptions& options = {}) const;
};

} // namespace spice::contentgraph
