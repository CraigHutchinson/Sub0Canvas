#include "sub0canvas/content_id.hpp"

#include <array>
#include <cstdint>

namespace sub0canvas {

namespace {

constexpr std::uint32_t rotl(std::uint32_t v, int n) { return (v << n) | (v >> (32 - n)); }

/// A plain SHA-1 (RFC 3174), fed in pieces. Collision resistance is not what the id is trusted for: it names a blob the way git does.
class Sha1
{
public:
    void add(std::string_view bytes)
    {
        for (const char c : bytes) {
            block_[used_++] = static_cast<std::uint8_t>(c);
            if (used_ == block_.size()) { process(); used_ = 0; }
        }
        size_ += bytes.size();
    }
    std::string hex()
    {
        const std::uint64_t bits = static_cast<std::uint64_t>(size_) * 8;
        const char pad = static_cast<char>(0x80);
        add(std::string_view{&pad, 1});
        const char zero = 0;
        while (used_ != 56) add(std::string_view{&zero, 1});
        for (int i = 7; i >= 0; --i) {
            const char b = static_cast<char>((bits >> (8 * i)) & 0xff);
            add(std::string_view{&b, 1});
        }
        std::string out;
        for (const std::uint32_t h : h_)
            for (int i = 7; i >= 0; --i) out += "0123456789abcdef"[(h >> (4 * i)) & 0xf];
        return out;
    }

private:
    void process()
    {
        std::array<std::uint32_t, 80> w{};
        for (int i = 0; i < 16; ++i)
            w[static_cast<std::size_t>(i)] = (std::uint32_t{block_[static_cast<std::size_t>(i) * 4]} << 24) | (std::uint32_t{block_[static_cast<std::size_t>(i) * 4 + 1]} << 16) |
                                            (std::uint32_t{block_[static_cast<std::size_t>(i) * 4 + 2]} << 8) | std::uint32_t{block_[static_cast<std::size_t>(i) * 4 + 3]};
        for (std::size_t i = 16; i < 80; ++i) w[i] = rotl(w[i - 3] ^ w[i - 8] ^ w[i - 14] ^ w[i - 16], 1);
        std::uint32_t a = h_[0], b = h_[1], c = h_[2], d = h_[3], e = h_[4];
        for (std::size_t i = 0; i < 80; ++i) {
            std::uint32_t f, k;
            if (i < 20) { f = (b & c) | (~b & d); k = 0x5a827999; }
            else if (i < 40) { f = b ^ c ^ d; k = 0x6ed9eba1; }
            else if (i < 60) { f = (b & c) | (b & d) | (c & d); k = 0x8f1bbcdc; }
            else { f = b ^ c ^ d; k = 0xca62c1d6; }
            const std::uint32_t t = rotl(a, 5) + f + e + k + w[i];
            e = d; d = c; c = rotl(b, 30); b = a; a = t;
        }
        h_[0] += a; h_[1] += b; h_[2] += c; h_[3] += d; h_[4] += e;
    }
    std::array<std::uint32_t, 5> h_{0x67452301, 0xefcdab89, 0x98badcfe, 0x10325476, 0xc3d2e1f0};
    std::array<std::uint8_t, 64> block_{};
    std::size_t used_ = 0, size_ = 0;
};

}  // namespace

std::string contentId(std::string_view text)
{
    Sha1 sha;
    const std::string head = "blob " + std::to_string(text.size());
    sha.add(head);
    sha.add(std::string_view{"\0", 1});
    sha.add(text);
    return sha.hex();
}

}  // namespace sub0canvas
