#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

namespace fasttris {

using Sha256Digest = std::array<std::uint8_t,32>;

class Sha256 {
public:
    Sha256();
    void update(const void* data, std::size_t size);
    void update(std::string_view data) { update(data.data(), data.size()); }
    Sha256Digest finalBytes();
    std::string finalHex();

private:
    void transform(const std::uint8_t block[64]);
    std::array<std::uint32_t,8> state_{};
    std::array<std::uint8_t,64> buffer_{};
    std::size_t buffer_size_{};
    std::uint64_t total_bytes_{};
    bool finalized_{};
    Sha256Digest digest_{};
};

std::string sha256(std::string_view data);
std::string hexLower(const std::uint8_t* data, std::size_t size);
inline std::string hexLower(const Sha256Digest& digest) { return hexLower(digest.data(), digest.size()); }
bool parseHex32(std::string_view hex, Sha256Digest& out);

} // namespace fasttris
