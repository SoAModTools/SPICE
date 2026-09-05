#include "StdJsonExporter.h"

#include <iomanip>
#include <sstream>
#include <type_traits>

namespace spice::stdfile {
namespace {

std::string jsonEscape(const std::string& value) {
    std::string escaped{};
    escaped.reserve(value.size() + 8U);
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

template <typename Range>
std::string bytesHex(const Range& bytes) {
    std::ostringstream out;
    out << std::hex << std::setfill('0');
    for (const auto byte : bytes) out << std::setw(2) << static_cast<unsigned>(byte);
    return out.str();
}

const char* endianName(const spice::root::Endian endian) {
    return endian == spice::root::Endian::Little ? "little" : "big";
}

void writeDiagnostics(std::ostringstream& out, const std::vector<StdDocumentDiagnostic>& diagnostics) {
    out << '[';
    for (std::size_t index = 0U; index < diagnostics.size(); ++index) {
        if (index != 0U) out << ',';
        const auto& diagnostic = diagnostics[index];
        out << "{\"code\":\"" << toString(diagnostic.code)
            << "\",\"severity\":\"" << toString(diagnostic.severity)
            << "\",\"decodedOffset\":";
        if (diagnostic.decodedOffset.has_value()) out << *diagnostic.decodedOffset;
        else out << "null";
        out << ",\"text\":\"" << jsonEscape(diagnostic.message) << "\"}";
    }
    out << ']';
}

template <typename Id>
void writeIds(std::ostringstream& out, const std::vector<Id>& ids) {
    out << '[';
    for (std::size_t index = 0U; index < ids.size(); ++index) {
        if (index != 0U) out << ',';
        out << ids[index].value;
    }
    out << ']';
}

void writeReceipt(std::ostringstream& out, const StdImportReceipt& receipt) {
    out << "{\"path\":";
    if (receipt.path.has_value()) out << '"' << jsonEscape(receipt.path->string()) << '"';
    else out << "null";
    out << ",\"sourceSha256\":\"" << bytesHex(receipt.sourceSha256)
        << "\",\"sourceSize\":" << receipt.sourceSize
        << ",\"decodedSize\":" << receipt.decodedSize
        << ",\"compression\":\"" << toString(receipt.compression)
        << "\",\"byteOrder\":\"" << endianName(receipt.byteOrder)
        << "\",\"byteOrderSelection\":\"" << toString(receipt.byteOrderSelection)
        << "\",\"opaqueEvidence\":{\"payloadIds\":";
    writeIds(out, receipt.opaqueEvidence.payloadIds);
    out << ",\"fragmentIds\":";
    writeIds(out, receipt.opaqueEvidence.fragmentIds);
    out << ",\"topLevelDecodedSha256\":";
    if (receipt.opaqueEvidence.topLevelDecodedSha256.has_value()) {
        out << '"' << bytesHex(*receipt.opaqueEvidence.topLevelDecodedSha256) << '"';
    } else {
        out << "null";
    }
    out << "}}";
}

void writeActionView(std::ostringstream& out, const StdActionViewPayload& value) {
    out << "{\"kind\":\"actionView\",\"primaryActionKey\":" << value.primaryActionKey
        << ",\"routeSecondaryKey\":" << value.routeSecondaryKey
        << ",\"directSecondaryKey\":" << value.directSecondaryKey
        << ",\"rawLowFlags\":" << value.rawLowFlags
        << ",\"raw08\":" << value.raw08
        << ",\"raw0c\":" << value.raw0c
        << ",\"actionViewFlags\":" << value.actionViewFlags
        << ",\"modeLocalValueBits\":" << value.modeLocalValueBits
        << ",\"startFrame\":" << value.startFrame
        << ",\"raw1a\":" << value.raw1a
        << ",\"endFrame\":" << value.endFrame
        << ",\"holdFrameCount\":" << value.holdFrameCount
        << ",\"stepFrameCount\":" << value.stepFrameCount
        << ",\"requestedMode\":" << value.requestedMode << '}';
}

void writeDocument(std::ostringstream& out, const StdDocument& document) {
    std::visit([&](const auto& content) {
        using Content = std::decay_t<decltype(content)>;
        if constexpr (std::is_same_v<Content, StdActionRowsContent>) {
            out << "{\"kind\":\"actionRows\",\"rawCommandLow\":" << content.rawCommandLow
                << ",\"rawCommandHigh\":" << content.rawCommandHigh
                << ",\"rawLoaderContextWord\":" << content.rawLoaderContextWord
                << ",\"rawRowTablePointerWord\":" << content.rawRowTablePointerWord
                << ",\"rows\":[";
            for (std::size_t index = 0U; index < content.rows.size(); ++index) {
                if (index != 0U) out << ',';
                const auto& row = content.rows[index];
                out << "{\"id\":" << row.id.value << ",\"actionId\":" << row.actionId
                    << ",\"rowType\":" << row.rowType
                    << ",\"callbackIndex\":" << row.callbackIndex
                    << ",\"raw06\":" << row.raw06
                    << ",\"flags\":" << row.flags
                    << ",\"secondaryKey\":" << row.secondaryKey
                    << ",\"raw0e\":" << row.raw0e
                    << ",\"raw10Bits\":" << row.raw10Bits
                    << ",\"raw14Bits\":" << row.raw14Bits << '}';
            }
            out << "]}";
        } else if constexpr (std::is_same_v<Content, StdEntryTableContent>) {
            out << "{\"kind\":\"entryTable\",\"tableKind\":" << content.kind
                << ",\"rawHeader04\":" << content.rawHeader04
                << ",\"rawHeader08\":" << content.rawHeader08
                << ",\"records\":[";
            for (std::size_t index = 0U; index < content.records.size(); ++index) {
                if (index != 0U) out << ',';
                const auto& record = content.records[index];
                out << "{\"id\":" << record.id.value << ",\"location\":" << record.locationCode
                    << ",\"opcode\":" << record.opcode << ",\"raw04\":" << record.raw04
                    << ",\"payloadId\":";
                if (record.payload.has_value()) out << record.payload->value;
                else out << "null";
                out << '}';
            }
            const auto& terminal = content.terminator;
            out << "],\"terminator\":{\"id\":" << terminal.id.value
                << ",\"negativeLocation\":" << terminal.negativeLocation
                << ",\"raw02\":" << terminal.raw02 << ",\"raw04\":" << terminal.raw04
                << ",\"raw08\":" << terminal.raw08 << ",\"raw0c\":" << terminal.raw0c
                << "},\"payloads\":[";
            for (std::size_t index = 0U; index < content.payloads.size(); ++index) {
                if (index != 0U) out << ',';
                const auto& payload = content.payloads[index];
                out << "{\"id\":" << payload.id.value << ",\"content\":";
                if (const auto* typed = std::get_if<StdActionViewPayload>(&payload.content)) writeActionView(out, *typed);
                else out << "{\"kind\":\"opaque\",\"bytesHex\":\"" << bytesHex(std::get<StdOpaquePayload>(payload.content).bytes) << "\"}";
                out << '}';
            }
            out << "],\"opaqueFragments\":[";
            for (std::size_t index = 0U; index < content.opaqueFragments.size(); ++index) {
                if (index != 0U) out << ',';
                const auto& fragment = content.opaqueFragments[index];
                out << "{\"id\":" << fragment.id.value << ",\"bytesHex\":\"" << bytesHex(fragment.bytes) << "\"}";
            }
            out << "],\"payloadLayout\":[";
            for (std::size_t index = 0U; index < content.payloadLayout.size(); ++index) {
                if (index != 0U) out << ',';
                std::visit([&](const auto id) {
                    using Id = std::remove_cv_t<decltype(id)>;
                    out << "{\"kind\":\"" << (std::is_same_v<Id, StdEntryPayloadId> ? "payload" : "opaqueFragment")
                        << "\",\"id\":" << id.value << '}';
                }, content.payloadLayout[index]);
            }
            out << "]}";
        } else {
            out << "{\"kind\":\"opaque\",\"decodedBytesHex\":\"" << bytesHex(content.decodedBytes) << "\"}";
        }
    }, document.content);
}

} // namespace

std::string StdJsonExporter::toJson(const StdDocumentImportResult& imported) const {
    std::ostringstream out;
    out << "{\n  \"schema\": \"spice_std_document_v2\",\n  \"ok\": " << (imported.ok() ? "true" : "false")
        << ",\n  \"receipt\": ";
    writeReceipt(out, imported.receipt);
    out << ",\n  \"diagnostics\": ";
    writeDiagnostics(out, imported.diagnostics);
    out << ",\n  \"document\": ";
    if (imported.document.has_value()) writeDocument(out, *imported.document);
    else out << "null";
    out << "\n}\n";
    return out.str();
}

std::string StdJsonExporter::toJson(const StdDocument& document) const {
    std::ostringstream out;
    out << "{\n  \"schema\": \"spice_std_document_v2\",\n  \"document\": ";
    writeDocument(out, document);
    out << "\n}\n";
    return out.str();
}

} // namespace spice::stdfile
