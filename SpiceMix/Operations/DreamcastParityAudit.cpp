#include "DreamcastParityAudit.h"

#include "../../Compression/Aklz.h"
#include "../../SpiceBin/BinParser.h"
#include "../../SpiceEct/EctParser.h"
#include "../../SpiceMLD/Parsing/MldParser.h"
#include "../../SpiceMlk/MlkParser.h"
#include "../../SpiceMll/MllParser.h"
#include "../../SpiceSCT/SctParser.h"
#include "../../SpiceSstSml/SstSmlDocumentAnalysis.h"
#include "../../SpiceSstSml/SstSmlDocumentImporter.h"
#include "../../SpiceSstSml/SstParser.h"
#include "../../SpiceStd/StdDocumentImporter.h"

#include <algorithm>
#include <atomic>
#include <bit>
#include <cctype>
#include <chrono>
#include <concepts>
#include <fstream>
#include <iomanip>
#include <map>
#include <set>
#include <sstream>
#include <stdexcept>
#include <unordered_map>
#include <type_traits>
#include <thread>

namespace spice::mix::detail {
namespace {

using spice::root::Endian;

struct Corpus {
    std::string id;
    std::string region;
    std::string platform;
    std::filesystem::path root;
    Endian expectedEndian = Endian::Big;
};

struct RecordRow {
    std::string kind;
    std::size_t index = 0U;
    std::string identity;
    std::string semantic;
};

struct Row {
    std::string corpus;
    std::string region;
    std::string platform;
    std::string format;
    std::string relativePath;
    std::string expectedEndian;
    std::string detectedEndian;
    bool automaticAgreement = false;
    bool parsed = false;
    std::string semantic;
    std::string diagnostics;
    std::vector<RecordRow> records;
};

class SemanticHasher {
public:
    template<std::integral Value>
    void add(const Value value) {
        addUnsigned(static_cast<std::uint64_t>(value));
    }

    void add(const std::string_view value) {
        addUnsigned(value.size());
        for (const unsigned char byte : value) {
            hash_ ^= byte;
            hash_ *= 1099511628211ULL;
        }
    }

    void add(const float value) { addUnsigned(std::bit_cast<std::uint32_t>(value)); }

    [[nodiscard]] std::string hex() const {
        std::ostringstream out;
        out << std::hex << std::setfill('0') << std::setw(16) << hash_;
        return out.str();
    }

private:
    void addUnsigned(const std::uint64_t value) {
        for (unsigned shift = 0U; shift < 64U; shift += 8U) {
            hash_ ^= static_cast<std::uint8_t>((value >> shift) & 0xffU);
            hash_ *= 1099511628211ULL;
        }
    }
    std::uint64_t hash_ = 14695981039346656037ULL;
};

void addList(SemanticHasher& hash, const std::shared_ptr<spice::mld::model::U32List>& list) {
    hash.add(list != nullptr);
    if (!list) return;
    hash.add(list->valid);
    hash.add(list->values.size());
    for (const auto value : list->values) hash.add(value);
}

std::string lower(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return value;
}

std::string formatForPath(const std::filesystem::path& path) {
    const auto extension = lower(path.extension().string());
    if (extension == ".sml") return "sst-sml";
    if (extension == ".std") return "std";
    if (extension == ".mlk") return "mlk";
    if (extension == ".mll") return "mll";
    if (extension == ".bin") return "bin-indexed";
    if (extension == ".mld") return "mld";
    if (extension == ".sct") return "sct";
    if (extension == ".ect") return "ect";
    return {};
}

std::string endianName(Endian endian) {
    return endian == Endian::Little ? "little" : "big";
}

std::string csv(std::string value) {
    if (value.find_first_of(",\"\r\n") == std::string::npos) return value;
    std::string escaped = "\"";
    for (char c : value) escaped += c == '"' ? "\"\"" : std::string(1, c);
    escaped += '"';
    return escaped;
}

std::string json(std::string value) {
    std::string escaped;
    for (char c : value) {
        switch (c) {
        case '\\': escaped += "\\\\"; break;
        case '"': escaped += "\\\""; break;
        case '\n': escaped += "\\n"; break;
        case '\r': escaped += "\\r"; break;
        case '\t': escaped += "\\t"; break;
        default: escaped += c; break;
        }
    }
    return escaped;
}

std::vector<std::uint8_t> readBytes(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary | std::ios::ate);
    if (!input) throw std::runtime_error("Unable to open corpus file: " + path.string());
    const auto end = input.tellg();
    if (end < 0) throw std::runtime_error("Unable to determine corpus file size: " + path.string());
    std::vector<std::uint8_t> bytes(static_cast<std::size_t>(end));
    input.seekg(0);
    if (!bytes.empty()) input.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    if (!input && !bytes.empty()) throw std::runtime_error("Unable to read corpus file: " + path.string());
    return bytes;
}

std::string diagnosticJoin(const auto& diagnostics) {
    std::ostringstream out;
    for (const auto& item : diagnostics) {
        if (out.tellp() > 0) out << " | ";
        out << item.message;
    }
    return out.str();
}

std::string joinDiagnostics(std::initializer_list<std::string> groups) {
    std::ostringstream out;
    for (const auto& group : groups) {
        if (group.empty()) continue;
        if (out.tellp() > 0) out << " | ";
        out << group;
    }
    return out.str();
}

std::string semanticMld(const spice::mld::model::MldFile& file) {
    SemanticHasher hash;
    hash.add(file.entries.size());
    for (const auto& record : file.entries) {
        const auto& entry = record.entry;
        hash.add(entry.entryId);
        hash.add(static_cast<std::uint32_t>(entry.tblId));
        hash.add(entry.fxnName);
        hash.add(entry.transform.position.x);
        hash.add(entry.transform.position.y);
        hash.add(entry.transform.position.z);
        hash.add(entry.transform.rotationRaw.x);
        hash.add(entry.transform.rotationRaw.y);
        hash.add(entry.transform.rotationRaw.z);
        hash.add(entry.transform.scale.x);
        hash.add(entry.transform.scale.y);
        hash.add(entry.transform.scale.z);
        addList(hash, entry.groundLinks);
        addList(hash, entry.paramList2);
        addList(hash, entry.functionParameters);
        addList(hash, entry.objectAddresses);
        addList(hash, entry.groundAddresses);
        addList(hash, entry.motionAddresses);
    }
    std::ostringstream out;
    out << "entries=" << file.entries.size()
        << ";objects=" << file.objectResources.size()
        << ";ground=" << file.groundResources.size()
        << ";motions=" << file.motionResources.size()
        << ";textures=" << (file.textureArchive ? file.textureArchive->entries.size() : 0U)
        << ";orderedEntryDigest=" << hash.hex();
    return out.str();
}

bool knownIndexedBin(std::string_view filename) {
    static const std::set<std::string> names{
        "hrsbincw.bin", "hrsbinpcwin.bin", "hrs_bend.bin", "hrsbin.bin"
    };
    return names.contains(lower(std::string(filename)));
}

std::filesystem::path companionSst(const std::filesystem::path& sml) {
    const auto wanted = lower(sml.stem().string());
    std::error_code error;
    for (std::filesystem::directory_iterator it(sml.parent_path(), error), end; !error && it != end; it.increment(error)) {
        if (it->is_regular_file(error) && !error
            && lower(it->path().extension().string()) == ".sst"
            && lower(it->path().stem().string()) == wanted) return it->path();
        error.clear();
    }
    return {};
}

Row inspect(const Corpus& corpus, const std::filesystem::path& path) {
    Row row{};
    row.corpus = corpus.id;
    row.region = corpus.region;
    row.platform = corpus.platform;
    row.relativePath = std::filesystem::relative(path, corpus.root).generic_string();
    row.expectedEndian = endianName(corpus.expectedEndian);
    const auto extension = lower(path.extension().string());
    const auto bytes = readBytes(path);

    if (extension == ".sml") {
        row.format = "sst-sml";
        const auto sst = companionSst(path);
        if (sst.empty()) { row.diagnostics = "same-stem SST companion missing"; return row; }
        const auto sstBytes = readBytes(sst);
        const auto imported = spice::sstsml::SstSmlDocumentImporter::importBytes(bytes, sstBytes);
        if (!imported.ok()) {
            row.diagnostics = diagnosticJoin(imported.diagnostics);
            return row;
        }
        const auto& document = *imported.document;
        const auto analysis = spice::sstsml::SstSmlDocumentAnalyzer::analyze(document, imported.receipt);
        row.parsed = analysis.ok();
        if (imported.receipt.sml.endian && imported.receipt.sst.endian) {
            row.detectedEndian = endianName(*imported.receipt.sml.endian);
            row.automaticAgreement = *imported.receipt.sml.endian == corpus.expectedEndian
                && *imported.receipt.sst.endian == corpus.expectedEndian;
        }
        SemanticHasher hash;
        hash.add(document.stageId);
        hash.add(document.members.size());
        for (std::size_t index = 0U; index < document.members.size(); ++index) {
            const auto& item = document.members[index];
            const auto& inspection = analysis.embeddedResources[index];
            hash.add(item.sml.resourceIndexWord);
            hash.add(item.sml.reservedWord);
            hash.add(inspection.entryCount.value_or(0U));
            hash.add(inspection.textureArchiveCount.value_or(0U));
            hash.add(inspection.hasNjcm);
            hash.add(inspection.hasNjtl);
            hash.add(inspection.hasNmdm);
            hash.add(inspection.hasGcix || inspection.hasGbix);
            hash.add(inspection.hasGvrt || inspection.hasPvrt || inspection.hasPvmh);
            row.records.push_back({ "sml-record", index, std::to_string(item.id.value),
                "embeddedMldEntries=" + std::to_string(inspection.entryCount.value_or(0U))
                    + ";embeddedMldAvailable=true" });
        }
        std::size_t commandRecordIndex = 0U;
        for (const auto& member : document.members) {
            const auto& block = member.sst.commandBlock;
            hash.add(block.commands.size());
            for (const auto& command : block.commands) {
                hash.add(command.type);
                hash.add(command.argument);
                hash.add(command.rawWord4);
                hash.add(command.rawWord8);
                hash.add(command.onDiskWord12);
                for (const auto& field : command.fields) {
                    std::visit([&](const auto value) { hash.add(value); }, field.value);
                }
                hash.add(command.lightingRows.size());
                for (const auto& lighting : command.lightingRows) {
                    hash.add(lighting.state);
                    hash.add(lighting.classSelector);
                    hash.add(lighting.flags);
                    hash.add(lighting.runtimeSlotId);
                    for (const auto value : lighting.lightVector) hash.add(value);
                    for (const auto value : lighting.slotRgb) hash.add(value);
                    for (const auto value : lighting.globalRgb) hash.add(value);
                }
                row.records.push_back({ "sst-command", commandRecordIndex++,
                    std::to_string(member.id.value) + ":" + std::to_string(command.id.value),
                    "type=" + std::to_string(command.type) + ";argument=" + std::to_string(command.argument)
                        + ";payloadWords=" + std::to_string((command.payloadSpanKnown
                            ? spice::sstsml::detail::SstParser::commandPayloadSize(command.type) : 0U) / 4U)
                        + ";payloadAvailable=true" });
            }
        }
        std::ostringstream semantic;
        semantic << "records=" << document.members.size() << ";commands=";
        for (const auto& [type, count] : analysis.commandTypeHistogram) semantic << type << ':' << count << '|';
        semantic << ";mldEntries=";
        for (const auto& item : analysis.embeddedResources) {
            semantic << item.entryCount.value_or(0U) << '|';
        }
        semantic << ";orderedDigest=" << hash.hex();
        row.semantic = semantic.str();
        row.diagnostics = joinDiagnostics({ diagnosticJoin(imported.diagnostics), diagnosticJoin(analysis.diagnostics) });
        return row;
    }
    if (extension == ".std") {
        row.format = "std";
        const auto forced = spice::stdfile::StdDocumentImporter::importBytes(
            bytes, { .byteOrder = corpus.expectedEndian });
        const auto automatic = spice::stdfile::StdDocumentImporter::importBytes(bytes);
        row.parsed = forced.ok();
        if (automatic.ok()) {
            row.detectedEndian = endianName(automatic.receipt.byteOrder);
            row.automaticAgreement = automatic.receipt.byteOrder == corpus.expectedEndian;
        }
        SemanticHasher hash;
        std::size_t rowCount = 0U;
        std::size_t recordCount = 0U;
        std::string layout = "none";
        if (forced.document.has_value()) {
            if (const auto* actionRows = std::get_if<spice::stdfile::StdActionRowsContent>(&forced.document->content)) {
                layout = "action-rows";
                rowCount = actionRows->rows.size();
                hash.add(1U);
                for (std::size_t index = 0U; index < actionRows->rows.size(); ++index) {
                    const auto& item = actionRows->rows[index];
                    hash.add(item.actionId); hash.add(item.rowType); hash.add(item.callbackIndex);
                    hash.add(item.raw06); hash.add(item.flags); hash.add(item.secondaryKey);
                    hash.add(item.raw0e); hash.add(item.raw10Bits); hash.add(item.raw14Bits);
                    row.records.push_back({ "action-row", index, std::to_string(item.id.value),
                        "actionId=" + std::to_string(item.actionId)
                            + ";rowType=" + std::to_string(item.rowType)
                            + ";callback=" + std::to_string(item.callbackIndex)
                            + ";flags=" + std::to_string(item.flags) });
                }
            } else if (const auto* table = std::get_if<spice::stdfile::StdEntryTableContent>(&forced.document->content)) {
                layout = "entry-table";
                recordCount = table->records.size() + 1U;
                hash.add(2U);
                for (std::size_t index = 0U; index < table->records.size(); ++index) {
                    const auto& item = table->records[index];
                    hash.add(item.locationCode); hash.add(item.opcode); hash.add(item.raw04);
                    hash.add(item.combinedType()); hash.add(item.payload.has_value());
                    std::size_t payloadSize = 0U;
                    if (item.payload.has_value()) {
                        const auto* payload = spice::stdfile::findEntryPayload(*table, *item.payload);
                        if (const auto* actionView = payload == nullptr ? nullptr
                            : std::get_if<spice::stdfile::StdActionViewPayload>(&payload->content)) {
                            payloadSize = spice::stdfile::kStdActionViewPayloadSize;
                            hash.add(actionView->primaryActionKey); hash.add(actionView->routeSecondaryKey);
                            hash.add(actionView->directSecondaryKey); hash.add(actionView->rawLowFlags);
                            hash.add(actionView->actionViewFlags); hash.add(actionView->modeLocalValueBits);
                            hash.add(actionView->startFrame); hash.add(actionView->endFrame);
                            hash.add(actionView->holdFrameCount); hash.add(actionView->stepFrameCount);
                            hash.add(actionView->requestedMode);
                        } else if (payload != nullptr) {
                            payloadSize = std::get<spice::stdfile::StdOpaquePayload>(payload->content).bytes.size();
                        }
                    }
                    row.records.push_back({ "entry", index, std::to_string(item.id.value),
                        "combinedType=" + std::to_string(item.combinedType())
                            + ";payloadSize=" + std::to_string(payloadSize)
                            + ";payloadAvailable=" + std::to_string(item.payload.has_value()) });
                }
                const auto& terminal = table->terminator;
                hash.add(terminal.negativeLocation); hash.add(terminal.raw02); hash.add(terminal.raw04);
                hash.add(terminal.raw08); hash.add(terminal.raw0c);
                row.records.push_back({ "terminator", table->records.size(), std::to_string(terminal.id.value),
                    "negativeLocation=" + std::to_string(terminal.negativeLocation) });
            } else {
                layout = "opaque";
                hash.add(3U);
            }
        }
        std::ostringstream semantic;
        semantic << "layout=" << layout << ";rows=" << rowCount << ";records=" << recordCount
            << ";orderedDigest=" << hash.hex();
        row.semantic = semantic.str(); row.diagnostics = diagnosticJoin(forced.diagnostics); return row;
    }
    if (extension == ".mlk") {
        row.format = "mlk";
        const auto forced = spice::mlk::MlkParser::parse(bytes, path.string(), { .forcedEndian = corpus.expectedEndian });
        const auto automatic = spice::mlk::MlkParser::parse(bytes, path.string());
        row.parsed = forced.ok();
        if (automatic.ok()) { row.detectedEndian = endianName(automatic.sourceEndian); row.automaticAgreement = automatic.sourceEndian == corpus.expectedEndian; }
        std::map<std::string, std::size_t> kinds;
        SemanticHasher hash;
        for (const auto& item : forced.records) {
            ++kinds[spice::mlk::toString(item.payloadKind)];
            hash.add(item.key);
            hash.add(static_cast<std::uint32_t>(item.payloadKind));
            hash.add(item.embeddedMldHeader.plausible);
            hash.add(item.embeddedMldHeader.entryCount);
            row.records.push_back({ "member", item.index, std::to_string(item.key),
                "kind=" + std::string(spice::mlk::toString(item.payloadKind))
                    + ";payloadInBounds=" + std::to_string(item.payloadInBounds)
                    + ";embeddedMldEntries=" + std::to_string(item.embeddedMldHeader.entryCount) });
        }
        std::ostringstream semantic;
        semantic << "records=" << forced.selectedRecordCount
            << ";descriptors=" << forced.descriptorRecordCount
            << ";unavailable=" << forced.unavailableTrailingRecordCount
            << ";kinds=";
        for (const auto& [kind, count] : kinds) semantic << kind << ':' << count << '|';
        semantic << ";orderedDigest=" << hash.hex();
        row.semantic = semantic.str(); row.diagnostics = diagnosticJoin(forced.diagnostics); return row;
    }
    if (extension == ".mll") {
        row.format = "mll";
        const auto forced = spice::mll::MllParser::parse(bytes, path.string(), { .forcedEndian = corpus.expectedEndian });
        const auto automatic = spice::mll::MllParser::parse(bytes, path.string());
        row.parsed = forced.ok();
        if (automatic.ok()) { row.detectedEndian = endianName(automatic.sourceEndian); row.automaticAgreement = automatic.sourceEndian == corpus.expectedEndian; }
        std::map<std::string, std::size_t> kinds;
        SemanticHasher hash;
        for (const auto& item : forced.members) {
            ++kinds[spice::mll::toString(item.payloadKind)];
            hash.add(item.name);
            hash.add(static_cast<std::uint32_t>(item.payloadKind));
            hash.add(item.embeddedMldHeader.plausible);
            hash.add(item.embeddedMldHeader.entryCount);
            hash.add(item.indexedBinTableProbe.present);
            hash.add(item.indexedBinTableProbe.count);
            row.records.push_back({ "member", item.index, item.name,
                "kind=" + std::string(spice::mll::toString(item.payloadKind))
                    + ";payloadInBounds=" + std::to_string(item.payloadInBounds)
                    + ";embeddedMldEntries=" + std::to_string(item.embeddedMldHeader.entryCount)
                    + ";indexedBinRecords=" + std::to_string(item.indexedBinTableProbe.count) });
        }
        std::ostringstream semantic; semantic << "members=" << forced.selectedMemberCount << ";kinds=";
        for (const auto& [kind, count] : kinds) semantic << kind << ':' << count << '|';
        semantic << ";orderedDigest=" << hash.hex();
        row.semantic = semantic.str(); row.diagnostics = diagnosticJoin(forced.diagnostics); return row;
    }
    if (extension == ".bin" && knownIndexedBin(path.filename().string())) {
        row.format = "bin-indexed";
        const auto forced = spice::bin::parseBytes(bytes, path.string(), { .forcedEndian = corpus.expectedEndian });
        const auto automatic = spice::bin::parseBytes(bytes, path.string());
        row.parsed = forced.indexedTableProbe.present && forced.indexedTableProbe.offsetsInBounds;
        if (automatic.sourceEndian) { row.detectedEndian = endianName(*automatic.sourceEndian); row.automaticAgreement = *automatic.sourceEndian == corpus.expectedEndian; }
        SemanticHasher hash;
        hash.add(forced.indexedTableProbe.count);
        for (const auto& sample : forced.indexedTableProbe.samples) {
            hash.add(sample.word0); hash.add(sample.word4); hash.add(sample.word8);
            hash.add(sample.word12); hash.add(sample.word16); hash.add(sample.word20); hash.add(sample.word24);
            row.records.push_back({ "sample", sample.sampleIndex, std::to_string(sample.sampleIndex),
                "recordInBounds=" + std::to_string(sample.recordInBounds)
                    + ";word0=" + std::to_string(sample.word0)
                    + ";word4=" + std::to_string(sample.word4) });
        }
        std::ostringstream semantic; semantic << "records=" << forced.indexedTableProbe.count
            << ";monotonic=" << forced.indexedTableProbe.offsetsMonotonic
            << ";orderedDigest=" << hash.hex();
        row.semantic = semantic.str(); row.diagnostics = diagnosticJoin(forced.diagnostics); return row;
    }
    if (extension == ".mld") {
        row.format = "mld";
        const auto parsed = spice::mld::parsing::MldParser{}.parseBytes(bytes, {
            .preserveSourceBytes = false,
            .parseResources = false,
        });
        row.parsed = parsed.parseStatus != spice::mld::model::MldParseStatus::Failed;
        row.detectedEndian = endianName(parsed.endian); row.automaticAgreement = parsed.endian == corpus.expectedEndian;
        for (const auto& item : parsed.entries) {
            const auto listSize = [](const auto& list) { return list ? list->values.size() : 0U; };
            row.records.push_back({ "entry", item.entry.tableIndex, std::to_string(item.entry.entryId),
                "tableId=" + std::to_string(item.entry.tblId) + ";function=" + item.entry.fxnName
                    + ";groundLinks=" + std::to_string(listSize(item.entry.groundLinks))
                    + ";paramList2=" + std::to_string(listSize(item.entry.paramList2))
                    + ";functionParameters=" + std::to_string(listSize(item.entry.functionParameters))
                    + ";objectSlots=" + std::to_string(listSize(item.entry.objectAddresses))
                    + ";groundSlots=" + std::to_string(listSize(item.entry.groundAddresses))
                    + ";motionSlots=" + std::to_string(listSize(item.entry.motionAddresses)) });
        }
        row.semantic = semanticMld(parsed); row.diagnostics = diagnosticJoin(parsed.parseDiagnostics); return row;
    }
    if (extension == ".sct") {
        row.format = "sct";
        const auto parsed = spice::sct::SctParser{}.parse(bytes, path.string());
        row.parsed = parsed.parseOk; row.detectedEndian = lower(parsed.file.detectedEndian);
        row.automaticAgreement = row.detectedEndian == row.expectedEndian;
        SemanticHasher hash;
        std::size_t instructions = 0;
        std::size_t sectionRecordIndex = 0U;
        for (const auto& section : parsed.file.sections) {
            hash.add(section.id.name);
            instructions += section.instructions.size();
            std::size_t decodeFailures = 0U;
            for (const auto& instruction : section.instructions) {
                hash.add(instruction.opcode);
                hash.add(instruction.operands.size());
                for (const auto operand : instruction.operands) hash.add(operand);
                if (!instruction.decodeOk) ++decodeFailures;
            }
            row.records.push_back({ "section", sectionRecordIndex++, section.id.name,
                "instructionCount=" + std::to_string(section.instructions.size())
                    + ";decodeFailures=" + std::to_string(decodeFailures) });
        }
        std::ostringstream semantic; semantic << "sections=" << parsed.file.sections.size() << ";instructions=" << instructions
            << ";orderedDigest=" << hash.hex();
        row.semantic = semantic.str(); row.diagnostics = diagnosticJoin(parsed.diagnostics); return row;
    }
    if (extension == ".ect") {
        row.format = "ect";
        const auto parsed = spice::ect::EctParser::parseFile(path);
        row.parsed = parsed.ok();
        row.detectedEndian = spice::compression::aklz::isAklz(bytes) ? "big" : "little";
        row.automaticAgreement = row.parsed && row.detectedEndian == row.expectedEndian;
        if (parsed.file) {
            SemanticHasher hash;
            std::visit([&](const auto& content) {
                using Content = std::decay_t<decltype(content)>;
                if constexpr (std::is_same_v<Content, spice::ect::EctFlatContent>) {
                    for (std::size_t index = 0U; index < content.tables.size(); ++index) {
                        const auto& table = content.tables[index];
                        hash.add(table.stage); hash.add(table.overallEncounterRate);
                        for (const auto& encounter : table.encounters) {
                            hash.add(encounter.encounterId); hash.add(encounter.encounterRate);
                        }
                        row.records.push_back({ "table", index, std::to_string(table.stage),
                            "overallRate=" + std::to_string(table.overallEncounterRate) });
                    }
                    row.semantic = "tables=" + std::to_string(content.tables.size());
                } else {
                    for (std::size_t index = 0U; index < content.entries.size(); ++index) {
                        const auto& entry = content.entries[index];
                        hash.add(entry.title);
                        for (const auto& table : entry.tables) {
                            hash.add(table.stage); hash.add(table.overallEncounterRate);
                            for (const auto& encounter : table.encounters) {
                                hash.add(encounter.encounterId); hash.add(encounter.encounterRate);
                            }
                        }
                        row.records.push_back({ "entry", index, entry.title,
                            "tableCount=" + std::to_string(entry.tables.size()) });
                    }
                    row.semantic = "entries=" + std::to_string(content.entries.size());
                }
            }, parsed.file->content);
            row.semantic += ";orderedDigest=" + hash.hex();
        }
        row.diagnostics = diagnosticJoin(parsed.diagnostics); return row;
    }
    return row;
}

void writeText(const std::filesystem::path& path, const std::string& text) {
    std::filesystem::create_directories(path.parent_path());
    std::ofstream out(path, std::ios::binary);
    if (!out) throw std::runtime_error("Unable to create audit report: " + path.string());
    out << text;
    if (!out) throw std::runtime_error("Unable to write audit report: " + path.string());
}

} // namespace

int executeDreamcastParityAudit(const AuditDreamcastParityRequest& request, OperationContext& context) {
    std::vector<Corpus> corpora{
        { "dreamcast-us", "us", "dreamcast", request.dreamcastUs, Endian::Little },
        { "gamecube-us", "us", "gamecube", request.gameCubeUs, Endian::Big },
    };
    if (request.dreamcastEuDisc1) {
        corpora.push_back({ "dreamcast-eu-disc1", "eu", "dreamcast", *request.dreamcastEuDisc1, Endian::Little });
        corpora.push_back({ "dreamcast-eu-disc2", "eu", "dreamcast", *request.dreamcastEuDisc2, Endian::Little });
        corpora.push_back({ "gamecube-eu", "eu", "gamecube", *request.gameCubeEu, Endian::Big });
    }

    struct Task {
        std::size_t corpusIndex = 0U;
        std::filesystem::path path;
    };
    const std::set<std::string> extensions{ ".sml", ".std", ".mlk", ".mll", ".bin", ".mld", ".sct", ".ect" };
    std::vector<Task> tasks;
    for (std::size_t corpusIndex = 0U; corpusIndex < corpora.size(); ++corpusIndex) {
        const auto& corpus = corpora[corpusIndex];
        if (context.report) context.report({ EventLevel::Progress, "Scanning " + corpus.id + "." });
        std::error_code error;
        std::vector<std::filesystem::path> files;
        for (std::filesystem::recursive_directory_iterator it(corpus.root, error), end; !error && it != end; it.increment(error)) {
            if (context.stopToken.stop_requested()) return 1;
            if (!it->is_regular_file(error) || error) { error.clear(); continue; }
            const auto extension = lower(it->path().extension().string());
            if (extensions.contains(extension) && (extension != ".bin" || knownIndexedBin(it->path().filename().string()))) files.push_back(it->path());
        }
        if (error) throw std::runtime_error("Unable to enumerate corpus: " + corpus.root.string());
        std::sort(files.begin(), files.end());
        if (context.report) context.report({ EventLevel::Info, corpus.id + ": "
            + std::to_string(files.size()) + " target files." });
        for (auto& file : files) tasks.push_back({ corpusIndex, std::move(file) });
    }

    const auto hardwareThreads = std::max(1U, std::thread::hardware_concurrency());
    const auto workerCount = std::min<std::size_t>(16U, std::min<std::size_t>(hardwareThreads, tasks.size()));
    if (context.report) context.report({ EventLevel::Info, "Parsing " + std::to_string(tasks.size())
        + " target files with " + std::to_string(workerCount) + " workers." });
    std::vector<std::optional<Row>> results(tasks.size());
    std::atomic_size_t nextTask{ 0U };
    std::atomic_size_t completedTasks{ 0U };
    std::vector<std::jthread> workers;
    workers.reserve(workerCount);
    for (std::size_t worker = 0U; worker < workerCount; ++worker) {
        workers.emplace_back([&] {
            while (!context.stopToken.stop_requested()) {
                const auto index = nextTask.fetch_add(1U, std::memory_order_relaxed);
                if (index >= tasks.size()) break;
                const auto& task = tasks[index];
                const auto& corpus = corpora[task.corpusIndex];
                Row row{};
                try {
                    row = inspect(corpus, task.path);
                } catch (const std::exception& error) {
                    row.corpus = corpus.id;
                    row.region = corpus.region;
                    row.platform = corpus.platform;
                    row.format = formatForPath(task.path);
                    row.relativePath = std::filesystem::relative(task.path, corpus.root).generic_string();
                    row.expectedEndian = endianName(corpus.expectedEndian);
                    row.diagnostics = std::string("Parser exception: ") + error.what();
                } catch (...) {
                    row.corpus = corpus.id;
                    row.region = corpus.region;
                    row.platform = corpus.platform;
                    row.format = formatForPath(task.path);
                    row.relativePath = std::filesystem::relative(task.path, corpus.root).generic_string();
                    row.expectedEndian = endianName(corpus.expectedEndian);
                    row.diagnostics = "Parser exception: unknown native exception";
                }
                results[index] = std::move(row);
                completedTasks.fetch_add(1U, std::memory_order_release);
            }
        });
    }

    std::size_t lastReported = 0U;
    while (completedTasks.load(std::memory_order_acquire) < tasks.size()
        && !context.stopToken.stop_requested()) {
        const auto completed = completedTasks.load(std::memory_order_acquire);
        if (context.report && (completed >= lastReported + 100U || completed == tasks.size())) {
            context.report({ EventLevel::Progress, "Audit parse progress: " + std::to_string(completed)
                + "/" + std::to_string(tasks.size()) });
            lastReported = completed;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    workers.clear();
    if (context.stopToken.stop_requested()) return 1;

    std::vector<Row> rows;
    rows.reserve(results.size());
    for (auto& result : results) {
        if (result.has_value() && !result->format.empty()) rows.push_back(std::move(*result));
    }

    std::ostringstream filesCsv;
    filesCsv << "corpus,region,platform,format,relativePath,expectedEndian,detectedEndian,automaticAgreement,parsed,semantic,diagnostics\n";
    for (const auto& row : rows) filesCsv << csv(row.corpus) << ',' << row.region << ',' << row.platform << ',' << row.format << ','
        << csv(row.relativePath) << ',' << row.expectedEndian << ',' << row.detectedEndian << ',' << row.automaticAgreement << ','
        << row.parsed << ',' << csv(row.semantic) << ',' << csv(row.diagnostics) << '\n';

    struct Summary { std::size_t scanned=0, parsed=0, autoAgree=0; };
    std::map<std::pair<std::string, std::string>, Summary> summaries;
    for (const auto& row : rows) { auto& item = summaries[{row.corpus, row.format}]; ++item.scanned; item.parsed += row.parsed; item.autoAgree += row.automaticAgreement; }
    std::ostringstream summaryCsv; summaryCsv << "corpus,format,scanned,parsed,parseFailures,automaticEndianAgreement,automaticEndianDisagreement\n";
    for (const auto& [key, item] : summaries) summaryCsv << key.first << ',' << key.second << ',' << item.scanned << ',' << item.parsed << ','
        << item.scanned-item.parsed << ',' << item.autoAgree << ',' << item.scanned-item.autoAgree << '\n';

    std::ostringstream mismatches; mismatches << "region,dreamcastCorpus,gamecubeCorpus,format,relativePath,dreamcastParsed,gamecubeParsed,semanticMatch,dreamcastSemantic,gamecubeSemantic\n";
    std::ostringstream unmatched; unmatched << "region,corpus,platform,format,relativePath\n";
    for (const auto& region : { std::string("us"), std::string("eu") }) {
        std::map<std::string, const Row*> gc;
        for (const auto& row : rows) if (row.region == region && row.platform == "gamecube") gc[lower(row.format + "|" + row.relativePath)] = &row;
        std::set<std::string> seen;
        for (const auto& row : rows) if (row.region == region && row.platform == "dreamcast") {
            const auto key = lower(row.format + "|" + row.relativePath); seen.insert(key);
            const auto found = gc.find(key);
            if (found == gc.end()) { unmatched << region << ',' << row.corpus << ",dreamcast," << row.format << ',' << csv(row.relativePath) << '\n'; continue; }
            const auto& other = *found->second;
            const bool match = row.parsed && other.parsed && row.semantic == other.semantic;
            if (!match) mismatches << region << ',' << row.corpus << ',' << other.corpus << ',' << row.format << ',' << csv(row.relativePath) << ','
                << row.parsed << ',' << other.parsed << ',' << match << ',' << csv(row.semantic) << ',' << csv(other.semantic) << '\n';
        }
        for (const auto& [key, row] : gc) if (!seen.contains(key)) unmatched << region << ',' << row->corpus << ",gamecube," << row->format << ',' << csv(row->relativePath) << '\n';
    }

    std::ostringstream automaticEndian;
    automaticEndian << "corpus,region,platform,format,relativePath,expectedEndian,detectedEndian,automaticAgreement\n";
    std::ostringstream parserFailures;
    parserFailures << "corpus,region,platform,format,relativePath,expectedEndian,diagnostics\n";
    for (const auto& row : rows) {
        automaticEndian << csv(row.corpus) << ',' << row.region << ',' << row.platform << ',' << row.format << ','
            << csv(row.relativePath) << ',' << row.expectedEndian << ',' << row.detectedEndian << ','
            << row.automaticAgreement << '\n';
        if (!row.parsed) {
            parserFailures << csv(row.corpus) << ',' << row.region << ',' << row.platform << ',' << row.format << ','
                << csv(row.relativePath) << ',' << row.expectedEndian << ',' << csv(row.diagnostics) << '\n';
        }
    }

    std::map<std::string, const Row*> euDisc1;
    std::map<std::string, const Row*> euDisc2;
    for (const auto& row : rows) {
        if (row.corpus != "dreamcast-eu-disc1" && row.corpus != "dreamcast-eu-disc2") continue;
        const auto key = lower(row.format + "|" + row.relativePath);
        (row.corpus == "dreamcast-eu-disc1" ? euDisc1 : euDisc2)[key] = &row;
    }
    std::set<std::string> euKeys;
    for (const auto& [key, row] : euDisc1) euKeys.insert(key);
    for (const auto& [key, row] : euDisc2) euKeys.insert(key);
    std::ostringstream euCollapsed;
    euCollapsed << "format,relativePath,disc1Present,disc2Present,disc1Parsed,disc2Parsed,semanticConflict,disc1Semantic,disc2Semantic\n";
    std::ostringstream euConflicts;
    euConflicts << "format,relativePath,disc1Parsed,disc2Parsed,disc1Semantic,disc2Semantic\n";
    for (const auto& key : euKeys) {
        const auto first = euDisc1.find(key);
        const auto second = euDisc2.find(key);
        const Row* row1 = first == euDisc1.end() ? nullptr : first->second;
        const Row* row2 = second == euDisc2.end() ? nullptr : second->second;
        const Row* identity = row1 ? row1 : row2;
        const bool conflict = row1 && row2
            && (row1->parsed != row2->parsed || row1->semantic != row2->semantic);
        euCollapsed << identity->format << ',' << csv(identity->relativePath) << ','
            << (row1 != nullptr) << ',' << (row2 != nullptr) << ','
            << (row1 && row1->parsed) << ',' << (row2 && row2->parsed) << ',' << conflict << ','
            << csv(row1 ? row1->semantic : std::string{}) << ','
            << csv(row2 ? row2->semantic : std::string{}) << '\n';
        if (conflict) {
            euConflicts << identity->format << ',' << csv(identity->relativePath) << ','
                << row1->parsed << ',' << row2->parsed << ','
                << csv(row1->semantic) << ',' << csv(row2->semantic) << '\n';
        }
    }

    std::ostringstream manifest;
    manifest << "{\n  \"schema\":\"spice_dreamcast_parity_audit_v1\",\n  \"corpora\":[";
    for (std::size_t i=0; i<corpora.size(); ++i) manifest << (i ? "," : "") << "\n    {\"id\":\"" << corpora[i].id
        << "\",\"region\":\"" << corpora[i].region << "\",\"platform\":\"" << corpora[i].platform
        << "\",\"root\":\"" << json(corpora[i].root.string()) << "\"}";
    manifest << "\n  ],\n  \"filesInspected\":" << rows.size() << ",\n  \"reports\":[\"summary.csv\",\"files.csv\",\"automatic-endian.csv\",\"parser-failures.csv\",\"mismatches.csv\",\"unmatched.csv\",\"eu-collapsed.csv\",\"eu-disc-conflicts.csv\",\"formats/<format>/files.csv\",\"formats/<format>/records.csv\"]\n}\n";

    std::filesystem::create_directories(request.output);
    writeText(request.output / "manifest.json", manifest.str());
    writeText(request.output / "summary.csv", summaryCsv.str());
    writeText(request.output / "files.csv", filesCsv.str());
    writeText(request.output / "automatic-endian.csv", automaticEndian.str());
    writeText(request.output / "parser-failures.csv", parserFailures.str());
    writeText(request.output / "mismatches.csv", mismatches.str());
    writeText(request.output / "unmatched.csv", unmatched.str());
    writeText(request.output / "eu-collapsed.csv", euCollapsed.str());
    writeText(request.output / "eu-disc-conflicts.csv", euConflicts.str());
    std::set<std::string> formats;
    for (const auto& row : rows) formats.insert(row.format);
    for (const auto& format : formats) {
        std::ostringstream detail;
        detail << "corpus,region,platform,relativePath,expectedEndian,detectedEndian,automaticAgreement,parsed,semantic,diagnostics\n";
        for (const auto& row : rows) if (row.format == format) detail << csv(row.corpus) << ',' << row.region << ',' << row.platform << ','
            << csv(row.relativePath) << ',' << row.expectedEndian << ',' << row.detectedEndian << ',' << row.automaticAgreement << ','
            << row.parsed << ',' << csv(row.semantic) << ',' << csv(row.diagnostics) << '\n';
        writeText(request.output / "formats" / format / "files.csv", detail.str());

        std::ostringstream records;
        records << "corpus,region,platform,relativePath,recordKind,recordIndex,identity,semantic\n";
        for (const auto& row : rows) {
            if (row.format != format) continue;
            for (const auto& record : row.records) {
                records << csv(row.corpus) << ',' << row.region << ',' << row.platform << ','
                    << csv(row.relativePath) << ',' << csv(record.kind) << ',' << record.index << ','
                    << csv(record.identity) << ',' << csv(record.semantic) << '\n';
            }
        }
        writeText(request.output / "formats" / format / "records.csv", records.str());
    }
    if (context.report) {
        context.report({ EventLevel::Info, "Dreamcast parity audit completed." });
        context.report({ EventLevel::Info, "FilesProcessed=" + std::to_string(rows.size()) });
    }
    return 0;
}

} // namespace spice::mix::detail
