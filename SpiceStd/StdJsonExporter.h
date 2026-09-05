#pragma once

#include "StdDocumentImporter.h"

#include <string>

namespace spice::stdfile {

class StdJsonExporter {
public:
    [[nodiscard]] std::string toJson(const StdDocumentImportResult& imported) const;
    [[nodiscard]] std::string toJson(const StdDocument& document) const;
};

} // namespace spice::stdfile
