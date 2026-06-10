#include "Aklz.h"

#include <array>
#include <limits>
#include <vector>

namespace spice::compression::aklz {
namespace {

constexpr std::array<std::uint8_t, kMagicSize> kMagic = {
    'A', 'K', 'L', 'Z', '~', '?', 'Q', 'd', '=', 0xCC, 0xCC, 0xCD
};

constexpr std::uint32_t kWindowSize = 1u << 12u;
constexpr std::uint32_t kWindowMask = kWindowSize - 1u;
constexpr std::uint32_t kLengthMask = (1u << 4u) - 1u;
constexpr std::uint32_t kMinLength = 3u;
constexpr std::uint32_t kMaxLength = 18u;
constexpr int kInitialWindowStart = -18;
// AKLZ uses an LZSS-style 4 KiB ring buffer with a logical start position at 0xFEE.
// Offsets encoded in back-references are relative to that start.
constexpr std::uint32_t kWindowStart = 0xFEEu;

[[nodiscard]] std::uint32_t readBeU32(std::span<const std::uint8_t> input, std::size_t offset) {
    return (std::uint32_t(input[offset + 0]) << 24) |
        (std::uint32_t(input[offset + 1]) << 16) |
        (std::uint32_t(input[offset + 2]) << 8) |
        std::uint32_t(input[offset + 3]);
}

void writeBeU32(std::vector<std::uint8_t>& output, std::size_t offset, std::uint32_t value) {
    output[offset + 0] = static_cast<std::uint8_t>((value >> 24u) & 0xFFu);
    output[offset + 1] = static_cast<std::uint8_t>((value >> 16u) & 0xFFu);
    output[offset + 2] = static_cast<std::uint8_t>((value >> 8u) & 0xFFu);
    output[offset + 3] = static_cast<std::uint8_t>(value & 0xFFu);
}

class FlagReader {
public:
    explicit FlagReader(std::span<const std::uint8_t> input, std::size_t cursor)
        : m_input(input), m_cursor(cursor) {
    }

    [[nodiscard]] bool readBit(bool& outBit) {
        if (m_bitsRemaining == 0) {
            if (m_cursor >= m_input.size()) {
                return false;
            }
            m_current = m_input[m_cursor++];
            m_bitsRemaining = 8;
        }

        outBit = (m_current & 0x01u) != 0u;
        m_current >>= 1;
        --m_bitsRemaining;
        return true;
    }

    [[nodiscard]] bool readByte(std::uint8_t& outByte) {
        if (m_cursor >= m_input.size()) {
            return false;
        }
        outByte = m_input[m_cursor++];
        return true;
    }

private:
    std::span<const std::uint8_t> m_input{};
    std::size_t m_cursor{ 0 };
    std::uint8_t m_current{ 0 };
    std::uint8_t m_bitsRemaining{ 0 };
};

struct Match {
    int position{ 0 };
    std::uint32_t length{ 0 };
};

class SlidingDictionary {
public:
    SlidingDictionary() {
        auto& zeroPositions = m_positions[0];
        for (int position = kInitialWindowStart; position < 0; ++position) {
            zeroPositions.push_back(position);
        }
    }

    [[nodiscard]] Match search(std::span<const std::uint8_t> input, const int position) {
        Match best{};
        if (position < 0 || static_cast<std::size_t>(position) >= input.size() ||
            input.size() - static_cast<std::size_t>(position) < kMinLength) {
            return best;
        }

        const auto first = input[static_cast<std::size_t>(position)];
        removeOldEntries(first);
        const auto& candidates = m_positions[first];
        const auto start = m_starts[first];
        if (start >= candidates.size()) {
            return best;
        }

        for (std::size_t index = candidates.size(); index > start; --index) {
            const int candidate = candidates[index - 1u];
            const auto length = matchLength(input, position, candidate);
            if (length >= kMinLength && length >= best.length) {
                if (best.position < 0 && candidate < -1 && length == best.length) {
                    continue;
                }
                best = Match{ .position = candidate, .length = length };
                if (length == kMaxLength) {
                    break;
                }
            }
        }

        return best;
    }

    void addEntry(const std::uint8_t value, const int position) {
        auto& positions = m_positions[value];
        auto& start = m_starts[value];
        if (start > 0 && start * 2u >= positions.size()) {
            positions.erase(positions.begin(), positions.begin() + static_cast<std::ptrdiff_t>(start));
            start = 0;
        }
        positions.push_back(position);
    }

    void addEntryRange(std::span<const std::uint8_t> input, const int position, const std::uint32_t length) {
        for (std::uint32_t i = 0; i < length; ++i) {
            const auto sourceIndex = static_cast<std::size_t>(position) + i;
            addEntry(input[sourceIndex], position + static_cast<int>(i));
        }
    }

    void slideWindow(const std::uint32_t amount) {
        if (m_currentWindowLength == kWindowSize) {
            m_windowStart += static_cast<int>(amount);
            return;
        }

        if (m_currentWindowLength + amount <= kWindowSize) {
            m_currentWindowLength += amount;
            return;
        }

        const auto overflow = amount - (kWindowSize - m_currentWindowLength);
        m_currentWindowLength = kWindowSize;
        m_windowStart += static_cast<int>(overflow);
    }

private:
    void removeOldEntries(const std::uint8_t value) {
        const auto& positions = m_positions[value];
        auto& start = m_starts[value];
        while (start < positions.size() && positions[start] < m_windowStart) {
            ++start;
        }
    }

    [[nodiscard]] std::uint32_t matchLength(std::span<const std::uint8_t> input, const int position, const int candidate) const {
        std::uint32_t length = 1;
        const auto inputSize = static_cast<int>(input.size());
        while (length < kMaxLength &&
            length < m_currentWindowLength &&
            position + static_cast<int>(length) < inputSize) {
            const int candidateIndex = candidate + static_cast<int>(length);
            const auto current = input[static_cast<std::size_t>(position + static_cast<int>(length))];
            if (candidateIndex > position) {
                const auto history = input[static_cast<std::size_t>(candidateIndex)];
                if (history != 0U && history != current) {
                    break;
                }
            }

            if (candidateIndex >= 0) {
                if (current != input[static_cast<std::size_t>(candidateIndex)]) {
                    break;
                }
            } else if (current != 0U) {
                break;
            }

            ++length;
        }
        return length;
    }

    std::array<std::vector<int>, 256> m_positions{};
    std::array<std::size_t, 256> m_starts{};
    int m_windowStart{ kInitialWindowStart };
    std::uint32_t m_currentWindowLength{ 33u };
};

} // namespace

std::string_view errorToString(AklzError error) {
    switch (error) {
    case AklzError::Ok: return "ok";
    case AklzError::InputTooSmall: return "input too small for AKLZ header";
    case AklzError::InputTooLarge: return "AKLZ input size exceeds configured cap";
    case AklzError::InvalidMagic: return "invalid AKLZ magic";
    case AklzError::TruncatedHeader: return "truncated AKLZ header";
    case AklzError::DecompressedSizeTooLarge: return "AKLZ decompressed size exceeds configured cap";
    case AklzError::TruncatedFlagStream: return "AKLZ flag stream truncated";
    case AklzError::TruncatedLiteral: return "AKLZ literal read truncated";
    case AklzError::TruncatedBackReference: return "AKLZ back-reference read truncated";
    case AklzError::OutputSizeMismatch: return "AKLZ output size mismatch";
    default: return "unknown AKLZ error";
    }
}

bool isAklz(std::span<const std::uint8_t> input) {
    if (input.size() < kMagicSize) {
        return false;
    }

    for (std::size_t i = 0; i < kMagicSize; ++i) {
        if (input[i] != kMagic[i]) {
            return false;
        }
    }

    return true;
}

AklzSizeResult getDecompressedSize(std::span<const std::uint8_t> input, std::uint32_t maxAllowedSize) {
    if (input.size() < kMagicSize) {
        return { .error = AklzError::InputTooSmall };
    }

    if (!isAklz(input)) {
        return { .error = AklzError::InvalidMagic };
    }

    if (input.size() < kHeaderSize) {
        return { .error = AklzError::TruncatedHeader };
    }

    const auto decompressedSize = readBeU32(input, kMagicSize);
    if (decompressedSize > maxAllowedSize) {
        return { .error = AklzError::DecompressedSizeTooLarge, .decompressedSize = decompressedSize };
    }

    return { .error = AklzError::Ok, .decompressedSize = decompressedSize };
}

AklzDecodeResult decompress(std::span<const std::uint8_t> input, std::uint32_t maxAllowedSize) {
    const auto sz = getDecompressedSize(input, maxAllowedSize);
    if (!sz.ok()) {
        return { .error = sz.error };
    }

    std::vector<std::uint8_t> output;
    output.reserve(sz.decompressedSize);

    std::array<std::uint8_t, kWindowSize> window{0};
    std::uint32_t windowWritePos = 0;

    FlagReader reader(input, kHeaderSize);

    while (output.size() < sz.decompressedSize) {
        bool literal = false;
        if (!reader.readBit(literal)) {
            return { .error = AklzError::TruncatedFlagStream };
        }

        if (literal) {
            std::uint8_t value = 0;
            if (!reader.readByte(value)) {
                return { .error = AklzError::TruncatedLiteral };
            }
            output.push_back(value);
            window[windowWritePos] = value;
            windowWritePos = (windowWritePos + 1u) & kWindowMask;
            continue;
        }

        std::uint8_t b1 = 0;
        std::uint8_t b2 = 0;
        if (!reader.readByte(b1) || !reader.readByte(b2)) {
            return { .error = AklzError::TruncatedBackReference };
        }

        std::uint32_t offset = ((std::uint32_t(b2) >> 4u) << 8u) | std::uint32_t(b1);
        const std::uint32_t length = (std::uint32_t(b2) & kLengthMask) + kMinLength;
        offset = (kWindowSize + offset - kWindowStart) & kWindowMask;

        for (std::uint32_t i = 0; i < length && output.size() < sz.decompressedSize; ++i) {
            const auto sourceIndex = (offset + i) & kWindowMask;

            std::uint8_t value = window[sourceIndex];

            output.push_back(value);
            window[windowWritePos] = value;
            windowWritePos = (windowWritePos + 1u) & kWindowMask;
        }
    }

    if (output.size() != sz.decompressedSize) {
        return { .error = AklzError::OutputSizeMismatch };
    }

    return { .error = AklzError::Ok, .bytes = std::move(output) };
}

AklzEncodeResult compress(std::span<const std::uint8_t> input, std::uint32_t maxAllowedSize) {
    if (input.size() > maxAllowedSize ||
        input.size() > static_cast<std::size_t>(std::numeric_limits<int>::max()) ||
        input.size() > static_cast<std::size_t>(std::numeric_limits<std::uint32_t>::max())) {
        return { .error = AklzError::InputTooLarge };
    }

    std::vector<std::uint8_t> output;
    output.reserve(kHeaderSize + input.size());
    output.insert(output.end(), kMagic.begin(), kMagic.end());
    output.resize(kHeaderSize, 0U);
    writeBeU32(output, kMagicSize, static_cast<std::uint32_t>(input.size()));

    SlidingDictionary dictionary;
    int inputPosition = 0;
    while (static_cast<std::size_t>(inputPosition) < input.size()) {
        const auto flagOffset = output.size();
        output.push_back(0U);
        std::uint8_t flagByte = 0U;

        for (std::uint8_t bit = 0; bit < 8U && static_cast<std::size_t>(inputPosition) < input.size(); ++bit) {
            const auto match = dictionary.search(input, inputPosition);
            if (match.length > 0U) {
                const int encodedOffset = match.position - static_cast<int>(kMaxLength);
                const auto encodedLength = static_cast<std::uint8_t>(match.length - kMinLength);
                output.push_back(static_cast<std::uint8_t>(encodedOffset & 0xFF));
                output.push_back(static_cast<std::uint8_t>(((encodedOffset & 0xF00) >> 4) | encodedLength));
                dictionary.addEntryRange(input, inputPosition, match.length);
                dictionary.slideWindow(match.length);
                inputPosition += static_cast<int>(match.length);
            } else {
                flagByte = static_cast<std::uint8_t>(flagByte | (1U << bit));
                output.push_back(input[static_cast<std::size_t>(inputPosition)]);
                dictionary.addEntry(input[static_cast<std::size_t>(inputPosition)], inputPosition);
                dictionary.slideWindow(1U);
                ++inputPosition;
            }
        }

        output[flagOffset] = flagByte;
    }

    return { .error = AklzError::Ok, .bytes = std::move(output) };
}

} // namespace spice::compression::aklz
