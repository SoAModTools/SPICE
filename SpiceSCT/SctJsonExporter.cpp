#include "SctJsonExporter.h"

#include <sstream>

namespace spice::sct {
namespace {

std::string jsonEscape(const std::string& value) {
    std::string escaped{};
    escaped.reserve(value.size() + 8);
    for (const char c : value) {
        switch (c) {
        case '\\': escaped += "\\\\"; break;
        case '"': escaped += "\\\""; break;
        case '\n': escaped += "\\n"; break;
        case '\r': escaped += "\\r"; break;
        case '\t': escaped += "\\t"; break;
        default: escaped.push_back(c); break;
        }
    }
    return escaped;
}

const char* toString(SctSemanticConfidence value) {
    switch (value) {
    case SctSemanticConfidence::Known: return "known";
    case SctSemanticConfidence::Partial: return "partial";
    case SctSemanticConfidence::Heuristic: return "heuristic";
    case SctSemanticConfidence::Unknown: return "unknown";
    default: return "unknown";
    }
}

const char* toString(SctSectionKind value) {
    switch (value) {
    case SctSectionKind::Script: return "script";
    case SctSectionKind::String: return "string";
    case SctSectionKind::Label: return "label";
    case SctSectionKind::Unknown: return "unknown";
    default: return "unknown";
    }
}

const char* toString(SctRawSpanReason value) {
    switch (value) {
    case SctRawSpanReason::Unreached: return "unreached";
    case SctRawSpanReason::PostReturn: return "post_return";
    case SctRawSpanReason::StringPadding: return "string_padding";
    case SctRawSpanReason::Unknown: return "unknown";
    default: return "unknown";
    }
}

const char* toString(SctParameterValueKind value) {
    switch (value) {
    case SctParameterValueKind::Raw: return "raw";
    case SctParameterValueKind::Integer: return "integer";
    case SctParameterValueKind::Expression: return "expression";
    case SctParameterValueKind::Link: return "link";
    case SctParameterValueKind::ResourceRef: return "resource_ref";
    case SctParameterValueKind::StringRef: return "string_ref";
    default: return "raw";
    }
}

const char* toString(SctScptAstNodeKind value) {
    switch (value) {
    case SctScptAstNodeKind::Unknown: return "unknown";
    case SctScptAstNodeKind::NoLoopValue: return "no_loop_value";
    case SctScptAstNodeKind::RawValue: return "raw_value";
    case SctScptAstNodeKind::FloatLiteral: return "float_literal";
    case SctScptAstNodeKind::DecimalLiteral: return "decimal_literal";
    case SctScptAstNodeKind::IntVariable: return "int_variable";
    case SctScptAstNodeKind::FloatVariable: return "float_variable";
    case SctScptAstNodeKind::BitVariable: return "bit_variable";
    case SctScptAstNodeKind::ByteVariable: return "byte_variable";
    case SctScptAstNodeKind::SecondaryValue: return "secondary_value";
    case SctScptAstNodeKind::CompareOp: return "compare_op";
    case SctScptAstNodeKind::ArithmeticOp: return "arithmetic_op";
    case SctScptAstNodeKind::AssignmentOp: return "assignment_op";
    case SctScptAstNodeKind::Stop: return "stop";
    default: return "unknown";
    }
}

const char* toString(SctEdgeType value) {
    switch (value) {
    case SctEdgeType::Fallthrough: return "fallthrough";
    case SctEdgeType::BranchTrue: return "branch_true";
    case SctEdgeType::BranchFalse: return "branch_false";
    case SctEdgeType::SwitchCase: return "switch_case";
    case SctEdgeType::Jump: return "jump";
    case SctEdgeType::CallSubscript: return "call_subscript";
    case SctEdgeType::Return: return "return";
    case SctEdgeType::LoadsScript: return "loads_script";
    case SctEdgeType::LoadsMld: return "loads_mld";
    case SctEdgeType::ReferencesString: return "references_string";
    default: return "fallthrough";
    }
}

void writeU32Array(std::ostringstream& out, const std::vector<std::uint32_t>& values) {
    out << '[';
    for (std::size_t i = 0; i < values.size(); ++i) {
        if (i != 0) {
            out << ',';
        }
        out << values[i];
    }
    out << ']';
}

void writeU8Array(std::ostringstream& out, const std::vector<std::uint8_t>& values) {
    out << '[';
    for (std::size_t i = 0; i < values.size(); ++i) {
        if (i != 0) {
            out << ',';
        }
        out << static_cast<unsigned int>(values[i]);
    }
    out << ']';
}

void writeOptionalU32(std::ostringstream& out, const std::optional<std::uint32_t>& value) {
    if (value.has_value()) {
        out << *value;
    } else {
        out << "null";
    }
}

void writeAttributes(std::ostringstream& out, const std::map<std::string, std::string>& attributes) {
    out << '{';
    std::size_t i = 0;
    for (const auto& [key, value] : attributes) {
        if (i++ != 0) {
            out << ',';
        }
        out << '"' << jsonEscape(key) << "\":\"" << jsonEscape(value) << '"';
    }
    out << '}';
}

void writeScptAstNode(std::ostringstream& out, const SctScptAstNode& node) {
    out << "{\"kind\":\"" << toString(node.kind)
        << "\",\"display\":\"" << jsonEscape(node.display)
        << "\",\"op\":\"" << jsonEscape(node.op)
        << "\",\"rawWords\":";
    writeU32Array(out, node.rawWords);
    out << ",\"children\":[";
    for (std::size_t i = 0; i < node.children.size(); ++i) {
        if (i != 0) {
            out << ',';
        }
        writeScptAstNode(out, node.children[i]);
    }
    out << "]}";
}

void writeExpression(std::ostringstream& out, const SctExpression& expression) {
    out << "{\"display\":\"" << jsonEscape(expression.display)
        << "\",\"hitStopCode\":" << (expression.hitStopCode ? "true" : "false")
        << ",\"trace\":[";
    for (std::size_t i = 0; i < expression.trace.size(); ++i) {
        if (i != 0) {
            out << ',';
        }
        out << "{\"rawWord\":" << expression.trace[i].rawWord
            << ",\"interpretedValue\":\"" << jsonEscape(expression.trace[i].interpretedValue) << "\"}";
    }
    out << "],\"ast\":";
    if (expression.ast.has_value()) {
        writeScptAstNode(out, *expression.ast);
    } else {
        out << "null";
    }
    out << '}';
}

void writeParameter(std::ostringstream& out, const SctParameter& parameter) {
    out << "{\"index\":" << parameter.index
        << ",\"role\":\"" << jsonEscape(parameter.role) << '"'
        << ",\"valueKind\":\"" << toString(parameter.valueKind) << '"'
        << ",\"confidence\":\"" << toString(parameter.confidence) << '"'
        << ",\"rawWords\":";
    writeU32Array(out, parameter.rawWords);
    out << ",\"displayValue\":\"" << jsonEscape(parameter.displayValue) << '"'
        << ",\"expression\":";
    if (parameter.expression.has_value()) {
        writeExpression(out, *parameter.expression);
    } else {
        out << "null";
    }
    out << '}';
}

} // namespace

std::string SctJsonExporter::toJson(const SctParseResult& result) const {
    std::ostringstream out;
    out << "{\n";
    out << "  \"schema\": \"spice_sct_ir_v1\",\n";
    out << "  \"source\": \"" << jsonEscape(result.file.sourcePath) << "\",\n";
    out << "  \"parseOk\": " << (result.parseOk ? "true" : "false") << ",\n";
    out << "  \"detectedEndian\": \"" << jsonEscape(result.file.detectedEndian) << "\",\n";
    out << "  \"sectionCount\": " << result.file.sections.size() << ",\n";
    out << "  \"sections\": [\n";
    for (std::size_t si = 0; si < result.file.sections.size(); ++si) {
        const auto& section = result.file.sections[si];
        if (si != 0) {
            out << ",\n";
        }
        out << "    {\n";
        out << "      \"index\": " << section.id.index << ",\n";
        out << "      \"name\": \"" << jsonEscape(section.id.name) << "\",\n";
        out << "      \"kind\": \"" << toString(section.kind) << "\",\n";
        out << "      \"startOffset\": " << section.startOffset << ",\n";
        out << "      \"endOffset\": " << section.endOffset << ",\n";
        out << "      \"rawSpans\": [";
        for (std::size_t ri = 0; ri < section.rawSpans.size(); ++ri) {
            const auto& span = section.rawSpans[ri];
            if (ri != 0) {
                out << ',';
            }
            out << "{\"startOffset\":" << span.startOffset
                << ",\"endOffset\":" << span.endOffset
                << ",\"reason\":\"" << toString(span.reason) << '"'
                << ",\"detail\":\"" << jsonEscape(span.detail) << '"'
                << ",\"rawBytes\":";
            writeU8Array(out, span.rawBytes);
            out << '}';
        }
        out << "],\n";

        out << "      \"instructions\": [";
        for (std::size_t ii = 0; ii < section.instructions.size(); ++ii) {
            const auto& inst = section.instructions[ii];
            if (ii != 0) {
                out << ',';
            }
            out << "{\"offset\":" << inst.offset
                << ",\"opcode\":" << inst.opcode
                << ",\"mnemonic\":\"" << jsonEscape(inst.mnemonic) << '"'
                << ",\"semanticConfidence\":\"" << toString(inst.semanticConfidence) << '"'
                << ",\"sizeBytes\":" << inst.sizeBytes
                << ",\"decodeOk\":" << (inst.decodeOk ? "true" : "false")
                << ",\"rawWords\":";
            writeU32Array(out, inst.rawWords);
            out << ",\"operands\":";
            writeU32Array(out, inst.operands);
            out << ",\"parameters\":[";
            for (std::size_t pi = 0; pi < inst.parameters.size(); ++pi) {
                if (pi != 0) {
                    out << ',';
                }
                writeParameter(out, inst.parameters[pi]);
            }
            out << "]}";
        }
        out << "],\n";

        out << "      \"blocks\": [";
        for (std::size_t bi = 0; bi < section.blocks.size(); ++bi) {
            const auto& block = section.blocks[bi];
            if (bi != 0) {
                out << ',';
            }
            out << "{\"startOffset\":" << block.startOffset
                << ",\"endOffset\":" << block.endOffset
                << ",\"instructionOffsets\":";
            writeU32Array(out, block.instructionOffsets);
            out << ",\"successorOffsets\":";
            writeU32Array(out, block.successorOffsets);
            out << '}';
        }
        out << "],\n";

        out << "      \"edges\": [";
        for (std::size_t ei = 0; ei < section.edges.size(); ++ei) {
            const auto& edge = section.edges[ei];
            if (ei != 0) {
                out << ',';
            }
            out << "{\"type\":\"" << toString(edge.type)
                << "\",\"confidence\":\"" << toString(edge.confidence)
                << "\",\"fromOffset\":";
            writeOptionalU32(out, edge.fromOffset);
            out << ",\"toOffset\":";
            writeOptionalU32(out, edge.toOffset);
            out << ",\"opcode\":" << edge.opcode
                << ",\"detail\":\"" << jsonEscape(edge.detail) << "\",\"attributes\":";
            writeAttributes(out, edge.attributes);
            out << '}';
        }
        out << "]\n";
        out << "    }";
    }
    out << "\n  ],\n";
    out << "  \"diagnostics\": [";
    for (std::size_t di = 0; di < result.diagnostics.size(); ++di) {
        if (di != 0) {
            out << ',';
        }
        const auto& diagnostic = result.diagnostics[di];
        out << "{\"section\":\"" << jsonEscape(diagnostic.section)
            << "\",\"offset\":" << diagnostic.offset
            << ",\"message\":\"" << jsonEscape(diagnostic.message) << "\"}";
    }
    out << "]\n";
    out << "}\n";
    return out.str();
}

} // namespace spice::sct
