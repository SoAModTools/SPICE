#include "SctSha256.h"

#include <array>
#include <bit>
#include <vector>

namespace spice::sct::detail {
namespace {

constexpr std::array<std::uint32_t, 64> kRound{
    0x428a2f98u,0x71374491u,0xb5c0fbcfu,0xe9b5dba5u,0x3956c25bu,0x59f111f1u,0x923f82a4u,0xab1c5ed5u,
    0xd807aa98u,0x12835b01u,0x243185beu,0x550c7dc3u,0x72be5d74u,0x80deb1feu,0x9bdc06a7u,0xc19bf174u,
    0xe49b69c1u,0xefbe4786u,0x0fc19dc6u,0x240ca1ccu,0x2de92c6fu,0x4a7484aau,0x5cb0a9dcu,0x76f988dau,
    0x983e5152u,0xa831c66du,0xb00327c8u,0xbf597fc7u,0xc6e00bf3u,0xd5a79147u,0x06ca6351u,0x14292967u,
    0x27b70a85u,0x2e1b2138u,0x4d2c6dfcu,0x53380d13u,0x650a7354u,0x766a0abbu,0x81c2c92eu,0x92722c85u,
    0xa2bfe8a1u,0xa81a664bu,0xc24b8b70u,0xc76c51a3u,0xd192e819u,0xd6990624u,0xf40e3585u,0x106aa070u,
    0x19a4c116u,0x1e376c08u,0x2748774cu,0x34b0bcb5u,0x391c0cb3u,0x4ed8aa4au,0x5b9cca4fu,0x682e6ff3u,
    0x748f82eeu,0x78a5636fu,0x84c87814u,0x8cc70208u,0x90befffau,0xa4506cebu,0xbef9a3f7u,0xc67178f2u};

constexpr std::uint32_t choose(std::uint32_t x, std::uint32_t y, std::uint32_t z) {
    return (x & y) ^ (~x & z);
}
constexpr std::uint32_t majority(std::uint32_t x, std::uint32_t y, std::uint32_t z) {
    return (x & y) ^ (x & z) ^ (y & z);
}

} // namespace

std::array<std::uint8_t, 32> sha256(std::span<const std::uint8_t> bytes) noexcept {
    std::vector<std::uint8_t> padded(bytes.begin(), bytes.end());
    const auto bitLength = static_cast<std::uint64_t>(bytes.size()) * 8u;
    padded.push_back(0x80u);
    while ((padded.size() % 64u) != 56u) padded.push_back(0u);
    for (int shift = 56; shift >= 0; shift -= 8) {
        padded.push_back(static_cast<std::uint8_t>(bitLength >> shift));
    }

    std::array<std::uint32_t, 8> state{
        0x6a09e667u,0xbb67ae85u,0x3c6ef372u,0xa54ff53au,
        0x510e527fu,0x9b05688cu,0x1f83d9abu,0x5be0cd19u};
    for (std::size_t block = 0; block < padded.size(); block += 64u) {
        std::array<std::uint32_t, 64> words{};
        for (std::size_t i = 0; i < 16u; ++i) {
            const auto at = block + i * 4u;
            words[i] = static_cast<std::uint32_t>(padded[at]) << 24u
                | static_cast<std::uint32_t>(padded[at + 1u]) << 16u
                | static_cast<std::uint32_t>(padded[at + 2u]) << 8u
                | padded[at + 3u];
        }
        for (std::size_t i = 16u; i < words.size(); ++i) {
            const auto s0 = std::rotr(words[i - 15u], 7) ^ std::rotr(words[i - 15u], 18)
                ^ (words[i - 15u] >> 3u);
            const auto s1 = std::rotr(words[i - 2u], 17) ^ std::rotr(words[i - 2u], 19)
                ^ (words[i - 2u] >> 10u);
            words[i] = words[i - 16u] + s0 + words[i - 7u] + s1;
        }
        auto [a,b,c,d,e,f,g,h] = state;
        for (std::size_t i = 0; i < words.size(); ++i) {
            const auto s1 = std::rotr(e, 6) ^ std::rotr(e, 11) ^ std::rotr(e, 25);
            const auto t1 = h + s1 + choose(e, f, g) + kRound[i] + words[i];
            const auto s0 = std::rotr(a, 2) ^ std::rotr(a, 13) ^ std::rotr(a, 22);
            const auto t2 = s0 + majority(a, b, c);
            h=g; g=f; f=e; e=d+t1; d=c; c=b; b=a; a=t1+t2;
        }
        state[0]+=a; state[1]+=b; state[2]+=c; state[3]+=d;
        state[4]+=e; state[5]+=f; state[6]+=g; state[7]+=h;
    }
    std::array<std::uint8_t, 32> result{};
    for (std::size_t i = 0; i < state.size(); ++i) {
        result[i*4u]=static_cast<std::uint8_t>(state[i]>>24u);
        result[i*4u+1u]=static_cast<std::uint8_t>(state[i]>>16u);
        result[i*4u+2u]=static_cast<std::uint8_t>(state[i]>>8u);
        result[i*4u+3u]=static_cast<std::uint8_t>(state[i]);
    }
    return result;
}

} // namespace spice::sct::detail
