#pragma once

#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace spice::sct {

enum class SctSemanticConfidence {
    Unknown,
    Heuristic,
    Partial,
    Known,
};

enum class SctSectionKind {
    Unknown,
    Script,
    String,
    Label,
};

enum class SctRawSpanReason {
    Unreached,
    PostReturn,
    StringPadding,
    Unknown,
};

enum class SctParameterValueKind {
    Raw,
    Integer,
    Expression,
    Link,
    ResourceRef,
    StringRef,
};

enum class SctEdgeType {
    Fallthrough,
    BranchTrue,
    BranchFalse,
    SwitchCase,
    Jump,
    CallSubscript,
    Return,
    LoadsScript,
    LoadsMld,
    ReferencesString,
};

struct SctSectionId {
    std::uint32_t index = 0;
    std::string name;
};

struct SctExpressionTraceEntry {
    std::uint32_t rawWord = 0;
    std::string interpretedValue;
};

enum class SctScptAstNodeKind {
    Unknown,
    NoLoopValue,
    RawValue,
    FloatLiteral,
    DecimalLiteral,
    IntVariable,
    FloatVariable,
    BitVariable,
    ByteVariable,
    SecondaryValue,
    CompareOp,
    ArithmeticOp,
    AssignmentOp,
    Stop,
};

struct SctScptAstNode {
    SctScptAstNodeKind kind = SctScptAstNodeKind::Unknown;
    std::string display;
    std::string op;
    std::vector<std::uint32_t> rawWords;
    std::vector<SctScptAstNode> children;
};

struct SctExpression {
    std::string display;
    std::vector<SctExpressionTraceEntry> trace;
    bool hitStopCode = false;
    std::optional<SctScptAstNode> ast;
};

struct SctParameter {
    std::uint32_t index = 0;
    std::string role;
    SctParameterValueKind valueKind = SctParameterValueKind::Raw;
    SctSemanticConfidence confidence = SctSemanticConfidence::Unknown;
    std::vector<std::uint32_t> rawWords;
    std::string displayValue;
    std::optional<SctExpression> expression;
};

struct SctScheduledInstructionMetadata {
    bool present = false;
    SctParameter frameDelay;
    std::uint32_t instructionByteLength = 0;
    std::vector<std::uint32_t> rawWords;
};

struct SctInstruction {
    struct ScptTraceEntry {
        std::uint32_t rawWord = 0;
        std::string interpretedValue;
    };

    struct ScptParameterValueRecord {
        std::uint8_t parameterIndex = 0;
        std::uint32_t operandStartWordIndex = 0;
        std::uint32_t operandWordCount = 0;
        bool hitStopCode = false;
        std::string resolvedValue;
        std::vector<ScptTraceEntry> evaluationTrace;
        std::optional<SctScptAstNode> ast;
    };

    std::uint32_t offset = 0;
    std::uint32_t payloadOffset = 0;
    std::uint16_t opcode = 0;
    std::uint32_t opcodeWordIndex = 0;
    bool skipRefresh = false;
    SctScheduledInstructionMetadata scheduled;
    std::string mnemonic;
    SctSemanticConfidence semanticConfidence = SctSemanticConfidence::Unknown;
    std::vector<std::uint32_t> operands;
    std::vector<std::uint32_t> rawWords;
    std::vector<SctParameter> parameters;
    std::vector<std::uint8_t> scptAnalyzeOperandIndexes;
    std::vector<ScptParameterValueRecord> scptParameterValueRecords;
    std::uint32_t sizeBytes = 0;
    bool decodeOk = false;
};

struct SctBasicBlock {
    std::uint32_t startOffset = 0;
    std::uint32_t endOffset = 0;
    std::vector<std::uint32_t> instructionOffsets;
    std::vector<std::uint32_t> successorOffsets;
};

struct SctUnknownRegion {
    std::uint32_t startOffset = 0;
    std::uint32_t endOffset = 0;
    std::vector<std::uint8_t> rawBytes;
    std::string reason;
};

struct SctRawSpan {
    std::uint32_t startOffset = 0;
    std::uint32_t endOffset = 0;
    SctRawSpanReason reason = SctRawSpanReason::Unknown;
    std::vector<std::uint8_t> rawBytes;
    std::string detail;
};

struct SctEdge {
    SctEdgeType type = SctEdgeType::Fallthrough;
    SctSemanticConfidence confidence = SctSemanticConfidence::Unknown;
    std::optional<std::uint32_t> fromOffset;
    std::optional<std::uint32_t> toOffset;
    std::optional<std::uint32_t> fromPayloadOffset;
    std::optional<std::uint32_t> toPayloadOffset;
    std::uint16_t opcode = 0;
    std::string detail;
    std::map<std::string, std::string> attributes;
};

struct SctCodeRegion {
    std::string name;
    std::uint32_t entryPayloadOffset = 0;
    std::vector<std::uint32_t> instructionPayloadOffsets;
    std::vector<std::uint32_t> coveredSectionIndexes;
};

struct FlagAccessSummary {
    std::vector<std::uint32_t> flagsRead;
    std::vector<std::uint32_t> flagsWritten;
    std::vector<std::uint32_t> flagsTested;
};

struct SectionHeuristicEvidence {
    bool touchesFlags = false;
    bool branchesOnFlags = false;
    bool writesFlags = false;
    bool hasSwitch = false;
    bool hasLongLinearSequence = false;
    bool hasPlayerReposition = false;
    bool hasCameraOrTimingLikeOps = false;
    bool likelyTrigger = false;
    bool likelyCutscene = false;
    std::vector<std::string> notes;
};

struct SctSection {
    SctSectionId id;
    std::uint32_t startOffset = 0;
    std::uint32_t endOffset = 0;
    SctSectionKind kind = SctSectionKind::Unknown;
    bool isStringSection = false;
    std::vector<SctInstruction> instructions;
    std::vector<SctBasicBlock> blocks;
    std::vector<SctEdge> edges;
    std::vector<SctUnknownRegion> unknownRegions;
    std::vector<SctRawSpan> rawSpans;
    FlagAccessSummary flagSummary;
    SectionHeuristicEvidence heuristicEvidence;
};

struct SctFile {
    std::string sourcePath;
    std::string detectedEndian;
    bool originalCompressedAklz = false;
    std::vector<std::uint8_t> originalBytes;
    std::vector<std::uint8_t> originalPayloadBytes;
    std::vector<std::uint8_t> headerBytes;
    std::vector<SctSection> sections;
    std::vector<SctCodeRegion> codeRegions;
};

struct SctDiagnostic {
    std::string message;
    std::uint32_t offset = 0;
    std::string section = {};
};

struct SctParseResult {
    SctFile file;
    std::vector<SctDiagnostic> diagnostics;
    bool parseOk = false;
};

} // namespace spice::sct
