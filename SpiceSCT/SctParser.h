#pragma once

#include "SctModel.h"

#include <cstdint>
#include <functional>
#include <span>
#include <string>

namespace spice::sct {

enum class SctParseTracePhase {
    Starting,
    Compression,
    SectionIndex,
    InstructionTraversal,
    Complete,
};

struct SctParseTraceEvent {
    SctParseTracePhase phase = SctParseTracePhase::Starting;
};

// Invoked synchronously on the parsing thread. Trace events are transient
// operation telemetry: they are never stored as document diagnostics.
using SctParseTraceObserver = std::function<void(const SctParseTraceEvent&)>;

struct SctParseOptions {
    bool decodeUnreachedCode = false;
    // Parsing performs no trace work and emits no console output when absent.
    SctParseTraceObserver traceObserver;
};

class SctParser {
public:
    [[nodiscard]] SctParseResult parse(
        std::span<const std::uint8_t> bytes,
        std::string sourcePath = {},
        SctParseOptions options = {}) const;
    [[nodiscard]] SctParseResult parseFile(
        const std::string& sourcePath,
        SctParseOptions options = {}) const;

private:
    [[nodiscard]] SctParseResult parseImpl(
        std::span<const std::uint8_t> bytes,
        std::string sourcePath,
        const SctParseOptions& options,
        bool emitStarting) const;
};

} // namespace spice::sct
