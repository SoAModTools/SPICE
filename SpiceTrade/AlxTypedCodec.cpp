#include "AlxTypedCodec.h"

#include <algorithm>
#include <array>
#include <charconv>
#include <cmath>
#include <fstream>
#include <iomanip>
#include <limits>
#include <locale>
#include <sstream>
#include <string>
#include <type_traits>
#include <utility>

namespace spice::trade::alx {
namespace {

template <typename Result>
void addReadDiagnostic(
    Result& result,
    std::string message,
    const std::filesystem::path& path,
    const std::optional<std::size_t> row = std::nullopt,
    const std::optional<std::size_t> column = std::nullopt)
{
    result.diagnostics.push_back(CsvDiagnostic{
        .severity = DiagnosticSeverity::Error,
        .message = std::move(message),
        .relativePath = path,
        .row = row,
        .column = column,
    });
}

void addWriteDiagnostic(
    CsvWriteResult& result,
    std::string message,
    const std::filesystem::path& path = {},
    const std::optional<std::size_t> row = std::nullopt,
    const std::optional<std::size_t> column = std::nullopt)
{
    result.diagnostics.push_back(CsvDiagnostic{
        .severity = DiagnosticSeverity::Error,
        .message = std::move(message),
        .relativePath = path,
        .row = row,
        .column = column,
    });
}

template <typename Result>
std::optional<AlxLocale> detectLocale(
    Result& result,
    const CsvDocument& document,
    const AlxTableKind kind,
    const std::filesystem::path& path)
{
    constexpr std::array locales{
        AlxLocale::Japanese,
        AlxLocale::UnitedStates,
        AlxLocale::Europe,
    };
    for (const auto locale : locales) {
        if (document.headers == canonicalHeaders(kind, locale)) {
            return locale;
        }
    }
    addReadDiagnostic(
        result,
        "CSV header does not exactly match an ALX 5.0.0 "
            + std::string(toString(kind)) + " JP, US, or EU schema",
        path,
        1U);
    return std::nullopt;
}

template <typename Integer>
bool parseDecimal(const std::string& text, Integer& output)
{
    static_assert(std::is_integral_v<Integer>);
    if (text.empty()) {
        return false;
    }

    if constexpr (std::is_signed_v<Integer>) {
        long long parsed{};
        const auto result = std::from_chars(text.data(), text.data() + text.size(), parsed, 10);
        if (result.ec != std::errc{} || result.ptr != text.data() + text.size()
            || parsed < static_cast<long long>(std::numeric_limits<Integer>::min())
            || parsed > static_cast<long long>(std::numeric_limits<Integer>::max())) {
            return false;
        }
        output = static_cast<Integer>(parsed);
    } else {
        unsigned long long parsed{};
        const auto result = std::from_chars(text.data(), text.data() + text.size(), parsed, 10);
        if (result.ec != std::errc{} || result.ptr != text.data() + text.size()
            || parsed > static_cast<unsigned long long>(std::numeric_limits<Integer>::max())) {
            return false;
        }
        output = static_cast<Integer>(parsed);
    }
    return true;
}

bool parseBinaryInt16(const std::string& text, std::int16_t& output)
{
    if (text.empty()) {
        return false;
    }
    std::size_t offset = 0U;
    bool negative = false;
    if (text[offset] == '-') {
        negative = true;
        ++offset;
    }
    if (offset + 2U > text.size() || text[offset] != '0'
        || (text[offset + 1U] != 'b' && text[offset + 1U] != 'B')) {
        return false;
    }
    offset += 2U;
    if (offset == text.size()) {
        return false;
    }

    unsigned long long magnitude{};
    const auto result = std::from_chars(text.data() + offset, text.data() + text.size(), magnitude, 2);
    if (result.ec != std::errc{} || result.ptr != text.data() + text.size()) {
        return false;
    }
    const auto signedValue = negative
        ? -static_cast<long long>(magnitude)
        : static_cast<long long>(magnitude);
    if (signedValue < std::numeric_limits<std::int16_t>::min()
        || signedValue > std::numeric_limits<std::int16_t>::max()) {
        return false;
    }
    output = static_cast<std::int16_t>(signedValue);
    return true;
}

bool parseFiniteFloat(const std::string& text, float& output)
{
    if (text.empty()) {
        return false;
    }
    const auto result = std::from_chars(
        text.data(), text.data() + text.size(), output, std::chars_format::general);
    return result.ec == std::errc{}
        && result.ptr == text.data() + text.size()
        && std::isfinite(output);
}

std::vector<std::string> splitFilters(const std::string& text)
{
    std::vector<std::string> filters{};
    if (text.empty()) {
        return filters;
    }
    std::size_t begin = 0U;
    while (begin <= text.size()) {
        const auto separator = text.find(';', begin);
        if (separator == std::string::npos) {
            filters.push_back(text.substr(begin));
            break;
        }
        filters.push_back(text.substr(begin, separator - begin));
        begin = separator + 1U;
        if (begin == text.size()) {
            break;
        }
    }
    return filters;
}

class RowReader {
public:
    RowReader(
        const CsvRow& row,
        std::vector<CsvDiagnostic>& diagnostics,
        std::filesystem::path path,
        const std::size_t rowNumber)
        : row_(row)
        , diagnostics_(diagnostics)
        , path_(std::move(path))
        , rowNumber_(rowNumber)
    {
    }

    std::string text()
    {
        return row_[column_++];
    }

    template <typename Integer>
    Integer integer()
    {
        const auto currentColumn = column_++;
        Integer value{};
        if (!parseDecimal(row_[currentColumn], value)) {
            error("Integer value is invalid or outside its ALX storage range", currentColumn);
        }
        return value;
    }

    std::int16_t binaryInt16()
    {
        const auto currentColumn = column_++;
        std::int16_t value{};
        if (!parseBinaryInt16(row_[currentColumn], value)) {
            error("Binary integer value is invalid or outside signed 16-bit range", currentColumn);
        }
        return value;
    }

    float finiteFloat()
    {
        const auto currentColumn = column_++;
        float value{};
        if (!parseFiniteFloat(row_[currentColumn], value)) {
            error("Floating-point value is invalid or non-finite", currentColumn);
        }
        return value;
    }

    LocalizedName localizedName(const AlxLocale locale)
    {
        LocalizedName name{};
        name.japanese = text();
        if (locale != AlxLocale::Japanese) {
            name.localized = text();
        }
        return name;
    }

private:
    void error(std::string message, const std::size_t column)
    {
        diagnostics_.push_back(CsvDiagnostic{
            .severity = DiagnosticSeverity::Error,
            .message = std::move(message),
            .relativePath = path_,
            .row = rowNumber_,
            .column = column + 1U,
        });
    }

    const CsvRow& row_;
    std::vector<CsvDiagnostic>& diagnostics_;
    std::filesystem::path path_{};
    std::size_t rowNumber_{};
    std::size_t column_{};
};

std::string formatBinaryInt16(const std::int16_t value)
{
    const auto signedValue = static_cast<std::int32_t>(value);
    const bool negative = signedValue < 0;
    auto magnitude = static_cast<std::uint32_t>(negative ? -signedValue : signedValue);
    std::string digits{};
    do {
        digits.push_back((magnitude & 1U) != 0U ? '1' : '0');
        magnitude >>= 1U;
    } while (magnitude != 0U);
    std::reverse(digits.begin(), digits.end());
    const std::size_t minimumDigits = negative ? 11U : 12U;
    if (digits.size() < minimumDigits) {
        digits.insert(digits.begin(), minimumDigits - digits.size(), '0');
    }
    return std::string(negative ? "-0b" : "0b") + digits;
}

std::string formatFloat1(const float value)
{
    const auto rounded = std::nearbyint(static_cast<double>(value) * 10.0) / 10.0;
    std::ostringstream stream{};
    stream.imbue(std::locale::classic());
    stream << std::fixed << std::setprecision(1) << rounded;
    return stream.str();
}

class RowWriter {
public:
    RowWriter(
        CsvWriteResult& result,
        const AlxLocale locale,
        const std::size_t rowNumber)
        : result_(result)
        , locale_(locale)
        , rowNumber_(rowNumber)
    {
    }

    void text(const std::string& value)
    {
        row_.push_back(value);
    }

    template <typename Integer>
    void integer(const Integer value)
    {
        if constexpr (std::is_signed_v<Integer>) {
            row_.push_back(std::to_string(static_cast<long long>(value)));
        } else {
            row_.push_back(std::to_string(static_cast<unsigned long long>(value)));
        }
    }

    void binaryInt16(const std::int16_t value)
    {
        row_.push_back(formatBinaryInt16(value));
    }

    void finiteFloat(const float value)
    {
        if (!std::isfinite(value)) {
            error("Floating-point value is non-finite");
            row_.push_back("0.0");
            return;
        }
        row_.push_back(formatFloat1(value));
    }

    void localizedName(const LocalizedName& name)
    {
        text(name.japanese);
        if (locale_ == AlxLocale::Japanese) {
            if (name.localized.has_value()) {
                error("JP schema cannot represent a localized US/EU name");
            }
            return;
        }
        if (!name.localized.has_value()) {
            error("US/EU schema requires a localized name field");
            text({});
            return;
        }
        text(*name.localized);
    }

    void filterList(const std::vector<std::string>& filters)
    {
        std::string joined{};
        for (std::size_t index = 0U; index < filters.size(); ++index) {
            if (filters[index].empty() || filters[index].find(';') != std::string::npos) {
                error("Enemy filter entries must be nonempty and cannot contain semicolons");
            }
            if (index != 0U) {
                joined.push_back(';');
            }
            joined += filters[index];
        }
        text(joined);
    }

    [[nodiscard]] CsvRow finish()
    {
        return std::move(row_);
    }

private:
    void error(std::string message)
    {
        addWriteDiagnostic(result_, std::move(message), {}, rowNumber_, row_.size() + 1U);
    }

    CsvWriteResult& result_;
    AlxLocale locale_{};
    std::size_t rowNumber_{};
    CsvRow row_{};
};

EnemyTableReadResult decodeEnemy(CsvReadResult csv, const std::filesystem::path& path)
{
    EnemyTableReadResult result{};
    result.format = csv.format;
    result.diagnostics = std::move(csv.diagnostics);
    if (!csv.ok()) {
        return result;
    }
    result.locale = detectLocale(result, *csv.document, AlxTableKind::Enemy, path);
    if (!result.locale.has_value()) {
        return result;
    }

    EnemyTable table{};
    table.records.reserve(csv.document->rows.size());
    for (std::size_t rowIndex = 0U; rowIndex < csv.document->rows.size(); ++rowIndex) {
        RowReader reader(csv.document->rows[rowIndex], result.diagnostics, path, rowIndex + 2U);
        EnemyRecord record{};
        record.entryId = reader.integer<std::uint32_t>();
        record.filters = splitFilters(reader.text());
        record.name = reader.localizedName(*result.locale);
        record.width = reader.integer<std::int8_t>();
        record.depth = reader.integer<std::int8_t>();
        record.elementId = reader.integer<std::int8_t>();
        record.elementName = reader.text();
        record.padding[0] = reader.integer<std::int8_t>();
        record.padding[1] = reader.integer<std::int8_t>();
        record.movementFlags = reader.binaryInt16();
        auto& movement = record.movementIndicators;
        movement.mayDodge = reader.text();
        movement.unknownDamage = reader.text();
        movement.unknownRanged = reader.text();
        movement.unknownMelee = reader.text();
        movement.rangedAttack = reader.text();
        movement.meleeAttack = reader.text();
        movement.rangedOnly = reader.text();
        movement.takeCover = reader.text();
        movement.inAir = reader.text();
        movement.onGround = reader.text();
        movement.reserved = reader.text();
        movement.mayMove = reader.text();
        record.counterPercent = reader.integer<std::int16_t>();
        record.experience = reader.integer<std::uint16_t>();
        record.gold = reader.integer<std::uint16_t>();
        record.padding[2] = reader.integer<std::int8_t>();
        record.padding[3] = reader.integer<std::int8_t>();
        record.maxHp = reader.integer<std::int32_t>();
        record.unknown1 = reader.finiteFloat();
        for (auto& value : record.elementValues) {
            value = reader.integer<std::int16_t>();
        }
        for (auto& value : record.stateValues) {
            value = reader.integer<std::int16_t>();
        }
        record.danger = reader.integer<std::int16_t>();
        record.effectId = reader.integer<std::int8_t>();
        record.effectName = reader.text();
        record.stateId = reader.integer<std::int8_t>();
        record.stateName = reader.text();
        record.stateMissPercent = reader.integer<std::int8_t>();
        record.padding[4] = reader.integer<std::int8_t>();
        record.level = reader.integer<std::int16_t>();
        record.will = reader.integer<std::int16_t>();
        record.vigor = reader.integer<std::int16_t>();
        record.agile = reader.integer<std::int16_t>();
        record.quick = reader.integer<std::int16_t>();
        record.attack = reader.integer<std::int16_t>();
        record.defense = reader.integer<std::int16_t>();
        record.magicDefense = reader.integer<std::int16_t>();
        record.hitPercent = reader.integer<std::int16_t>();
        record.dodgePercent = reader.integer<std::int16_t>();
        record.padding[5] = reader.integer<std::int8_t>();
        record.padding[6] = reader.integer<std::int8_t>();
        for (auto& item : record.itemDrops) {
            item.probability = reader.integer<std::int16_t>();
            item.amount = reader.integer<std::int16_t>();
            item.itemId = reader.integer<std::int16_t>();
            item.itemName = reader.text();
        }
        table.records.push_back(std::move(record));
    }
    if (!hasErrors(result.diagnostics)) {
        result.table = std::move(table);
    }
    return result;
}

EnemyEncounterTableReadResult decodeEncounter(CsvReadResult csv, const std::filesystem::path& path)
{
    EnemyEncounterTableReadResult result{};
    result.format = csv.format;
    result.diagnostics = std::move(csv.diagnostics);
    if (!csv.ok()) {
        return result;
    }
    result.locale = detectLocale(result, *csv.document, AlxTableKind::EnemyEncounter, path);
    if (!result.locale.has_value()) {
        return result;
    }

    EnemyEncounterTable table{};
    table.records.reserve(csv.document->rows.size());
    for (std::size_t rowIndex = 0U; rowIndex < csv.document->rows.size(); ++rowIndex) {
        RowReader reader(csv.document->rows[rowIndex], result.diagnostics, path, rowIndex + 2U);
        EnemyEncounterRecord record{};
        record.entryId = reader.integer<std::uint32_t>();
        record.filter = reader.text();
        record.initiative = reader.integer<std::uint8_t>();
        record.magicExperience = reader.integer<std::uint8_t>();
        for (auto& enemy : record.enemies) {
            enemy.enemyId = reader.integer<std::uint8_t>();
            enemy.name = reader.localizedName(*result.locale);
        }
        table.records.push_back(std::move(record));
    }
    if (!hasErrors(result.diagnostics)) {
        result.table = std::move(table);
    }
    return result;
}

EnemyEventTableReadResult decodeEvent(CsvReadResult csv, const std::filesystem::path& path)
{
    EnemyEventTableReadResult result{};
    result.format = csv.format;
    result.diagnostics = std::move(csv.diagnostics);
    if (!csv.ok()) {
        return result;
    }
    result.locale = detectLocale(result, *csv.document, AlxTableKind::EnemyEvent, path);
    if (!result.locale.has_value()) {
        return result;
    }

    EnemyEventTable table{};
    table.records.reserve(csv.document->rows.size());
    for (std::size_t rowIndex = 0U; rowIndex < csv.document->rows.size(); ++rowIndex) {
        RowReader reader(csv.document->rows[rowIndex], result.diagnostics, path, rowIndex + 2U);
        EnemyEventRecord record{};
        record.entryId = reader.integer<std::uint32_t>();
        record.magicExperience = reader.integer<std::uint8_t>();
        for (auto& player : record.players) {
            player.characterId = reader.integer<std::int8_t>();
            player.characterName = reader.text();
            player.x = reader.integer<std::int8_t>();
            player.z = reader.integer<std::int8_t>();
        }
        for (auto& enemy : record.enemies) {
            enemy.enemy.enemyId = reader.integer<std::uint8_t>();
            enemy.enemy.name = reader.localizedName(*result.locale);
            enemy.x = reader.integer<std::int8_t>();
            enemy.z = reader.integer<std::int8_t>();
        }
        record.initiative = reader.integer<std::uint8_t>();
        record.defeatCondition.conditionId = reader.integer<std::int8_t>();
        record.defeatCondition.conditionName = reader.text();
        record.escapeCondition.conditionId = reader.integer<std::int8_t>();
        record.escapeCondition.conditionName = reader.text();
        const auto bgmText = reader.text();
        if (bgmText == "-1") {
            record.bgmId = std::nullopt;
        } else {
            std::uint32_t bgmId{};
            if (!parseDecimal(bgmText, bgmId)) {
                result.diagnostics.push_back(CsvDiagnostic{
                    .severity = DiagnosticSeverity::Error,
                    .message = "BGM ID must be -1 or an unsigned 32-bit integer",
                    .relativePath = path,
                    .row = rowIndex + 2U,
                    .column = csv.document->headers.size(),
                });
            }
            record.bgmId = bgmId;
        }
        table.records.push_back(std::move(record));
    }
    if (!hasErrors(result.diagnostics)) {
        result.table = std::move(table);
    }
    return result;
}

CsvDocument encodeEnemy(
    const EnemyTable& table,
    const AlxLocale locale,
    CsvWriteResult& result)
{
    CsvDocument document{ .headers = canonicalHeaders(AlxTableKind::Enemy, locale) };
    document.rows.reserve(table.records.size());
    for (std::size_t rowIndex = 0U; rowIndex < table.records.size(); ++rowIndex) {
        const auto& record = table.records[rowIndex];
        RowWriter writer(result, locale, rowIndex + 2U);
        writer.integer(record.entryId);
        writer.filterList(record.filters);
        writer.localizedName(record.name);
        writer.integer(record.width);
        writer.integer(record.depth);
        writer.integer(record.elementId);
        writer.text(record.elementName);
        writer.integer(record.padding[0]);
        writer.integer(record.padding[1]);
        writer.binaryInt16(record.movementFlags);
        const auto& movement = record.movementIndicators;
        writer.text(movement.mayDodge);
        writer.text(movement.unknownDamage);
        writer.text(movement.unknownRanged);
        writer.text(movement.unknownMelee);
        writer.text(movement.rangedAttack);
        writer.text(movement.meleeAttack);
        writer.text(movement.rangedOnly);
        writer.text(movement.takeCover);
        writer.text(movement.inAir);
        writer.text(movement.onGround);
        writer.text(movement.reserved);
        writer.text(movement.mayMove);
        writer.integer(record.counterPercent);
        writer.integer(record.experience);
        writer.integer(record.gold);
        writer.integer(record.padding[2]);
        writer.integer(record.padding[3]);
        writer.integer(record.maxHp);
        writer.finiteFloat(record.unknown1);
        for (const auto value : record.elementValues) {
            writer.integer(value);
        }
        for (const auto value : record.stateValues) {
            writer.integer(value);
        }
        writer.integer(record.danger);
        writer.integer(record.effectId);
        writer.text(record.effectName);
        writer.integer(record.stateId);
        writer.text(record.stateName);
        writer.integer(record.stateMissPercent);
        writer.integer(record.padding[4]);
        writer.integer(record.level);
        writer.integer(record.will);
        writer.integer(record.vigor);
        writer.integer(record.agile);
        writer.integer(record.quick);
        writer.integer(record.attack);
        writer.integer(record.defense);
        writer.integer(record.magicDefense);
        writer.integer(record.hitPercent);
        writer.integer(record.dodgePercent);
        writer.integer(record.padding[5]);
        writer.integer(record.padding[6]);
        for (const auto& item : record.itemDrops) {
            writer.integer(item.probability);
            writer.integer(item.amount);
            writer.integer(item.itemId);
            writer.text(item.itemName);
        }
        document.rows.push_back(writer.finish());
    }
    return document;
}

CsvDocument encodeEncounter(
    const EnemyEncounterTable& table,
    const AlxLocale locale,
    CsvWriteResult& result)
{
    CsvDocument document{ .headers = canonicalHeaders(AlxTableKind::EnemyEncounter, locale) };
    document.rows.reserve(table.records.size());
    for (std::size_t rowIndex = 0U; rowIndex < table.records.size(); ++rowIndex) {
        const auto& record = table.records[rowIndex];
        RowWriter writer(result, locale, rowIndex + 2U);
        writer.integer(record.entryId);
        writer.text(record.filter);
        writer.integer(record.initiative);
        writer.integer(record.magicExperience);
        for (const auto& enemy : record.enemies) {
            writer.integer(enemy.enemyId);
            writer.localizedName(enemy.name);
        }
        document.rows.push_back(writer.finish());
    }
    return document;
}

CsvDocument encodeEvent(
    const EnemyEventTable& table,
    const AlxLocale locale,
    CsvWriteResult& result)
{
    CsvDocument document{ .headers = canonicalHeaders(AlxTableKind::EnemyEvent, locale) };
    document.rows.reserve(table.records.size());
    for (std::size_t rowIndex = 0U; rowIndex < table.records.size(); ++rowIndex) {
        const auto& record = table.records[rowIndex];
        RowWriter writer(result, locale, rowIndex + 2U);
        writer.integer(record.entryId);
        writer.integer(record.magicExperience);
        for (const auto& player : record.players) {
            writer.integer(player.characterId);
            writer.text(player.characterName);
            writer.integer(player.x);
            writer.integer(player.z);
        }
        for (const auto& enemy : record.enemies) {
            writer.integer(enemy.enemy.enemyId);
            writer.localizedName(enemy.enemy.name);
            writer.integer(enemy.x);
            writer.integer(enemy.z);
        }
        writer.integer(record.initiative);
        writer.integer(record.defeatCondition.conditionId);
        writer.text(record.defeatCondition.conditionName);
        writer.integer(record.escapeCondition.conditionId);
        writer.text(record.escapeCondition.conditionName);
        if (record.bgmId.has_value()) {
            writer.integer(*record.bgmId);
        } else {
            writer.text("-1");
        }
        document.rows.push_back(writer.finish());
    }
    return document;
}

template <typename Builder, typename Table>
CsvWriteResult encodeTable(
    const Table& table,
    const AlxLocale locale,
    const CsvFormat& format,
    Builder&& builder)
{
    CsvWriteResult result{};
    auto document = builder(table, locale, result);
    if (hasErrors(result.diagnostics)) {
        return result;
    }
    auto csv = CsvWriter{}.write(document, format);
    result.bytes = std::move(csv.bytes);
    result.diagnostics.insert(
        result.diagnostics.end(),
        std::make_move_iterator(csv.diagnostics.begin()),
        std::make_move_iterator(csv.diagnostics.end()));
    return result;
}

CsvWriteResult writeResultToFile(CsvWriteResult result, const std::filesystem::path& path)
{
    if (!result.ok()) {
        for (auto& diagnostic : result.diagnostics) {
            diagnostic.relativePath = path;
        }
        return result;
    }
    std::error_code error{};
    if (path.has_parent_path()) {
        std::filesystem::create_directories(path.parent_path(), error);
        if (error) {
            addWriteDiagnostic(result, "Could not create ALX CSV output directory", path);
            return result;
        }
    }
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    if (!output) {
        addWriteDiagnostic(result, "Could not open ALX CSV output file", path);
        return result;
    }
    output.write(
        reinterpret_cast<const char*>(result.bytes.data()),
        static_cast<std::streamsize>(result.bytes.size()));
    if (!output.good()) {
        addWriteDiagnostic(result, "Could not write ALX CSV output file", path);
    }
    return result;
}

} // namespace

EnemyTableReadResult EnemyCsvCodec::parse(
    const std::span<const std::uint8_t> bytes,
    const std::filesystem::path& diagnosticPath) const
{
    return decodeEnemy(CsvReader{}.parse(bytes, diagnosticPath), diagnosticPath);
}

EnemyTableReadResult EnemyCsvCodec::readFile(const std::filesystem::path& path) const
{
    return decodeEnemy(CsvReader{}.readFile(path), path);
}

CsvWriteResult EnemyCsvCodec::write(
    const EnemyTable& table,
    const AlxLocale locale,
    const CsvFormat& format) const
{
    return encodeTable(table, locale, format, encodeEnemy);
}

CsvWriteResult EnemyCsvCodec::writeFile(
    const EnemyTable& table,
    const AlxLocale locale,
    const std::filesystem::path& path,
    const CsvFormat& format) const
{
    return writeResultToFile(write(table, locale, format), path);
}

EnemyEncounterTableReadResult EnemyEncounterCsvCodec::parse(
    const std::span<const std::uint8_t> bytes,
    const std::filesystem::path& diagnosticPath) const
{
    return decodeEncounter(CsvReader{}.parse(bytes, diagnosticPath), diagnosticPath);
}

EnemyEncounterTableReadResult EnemyEncounterCsvCodec::readFile(const std::filesystem::path& path) const
{
    return decodeEncounter(CsvReader{}.readFile(path), path);
}

CsvWriteResult EnemyEncounterCsvCodec::write(
    const EnemyEncounterTable& table,
    const AlxLocale locale,
    const CsvFormat& format) const
{
    return encodeTable(table, locale, format, encodeEncounter);
}

CsvWriteResult EnemyEncounterCsvCodec::writeFile(
    const EnemyEncounterTable& table,
    const AlxLocale locale,
    const std::filesystem::path& path,
    const CsvFormat& format) const
{
    return writeResultToFile(write(table, locale, format), path);
}

EnemyEventTableReadResult EnemyEventCsvCodec::parse(
    const std::span<const std::uint8_t> bytes,
    const std::filesystem::path& diagnosticPath) const
{
    return decodeEvent(CsvReader{}.parse(bytes, diagnosticPath), diagnosticPath);
}

EnemyEventTableReadResult EnemyEventCsvCodec::readFile(const std::filesystem::path& path) const
{
    return decodeEvent(CsvReader{}.readFile(path), path);
}

CsvWriteResult EnemyEventCsvCodec::write(
    const EnemyEventTable& table,
    const AlxLocale locale,
    const CsvFormat& format) const
{
    return encodeTable(table, locale, format, encodeEvent);
}

CsvWriteResult EnemyEventCsvCodec::writeFile(
    const EnemyEventTable& table,
    const AlxLocale locale,
    const std::filesystem::path& path,
    const CsvFormat& format) const
{
    return writeResultToFile(write(table, locale, format), path);
}

} // namespace spice::trade::alx
