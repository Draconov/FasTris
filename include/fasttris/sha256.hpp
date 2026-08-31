#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

namespace fasttris {

class Sha256 {
public:
    Sha256();
    void update(const void* data, std::size_t size);
    void update(std::string_view data) { update(data.data(), data.size()); }
    std::array<std::uint8_t,32> finalBytes();
    std::string finalHex();

private:
    void transform(const std::uint8_t block[64]);
    std::array<std::uint32_t,8> state_{};
    std::array<std::uint8_t,64> buffer_{};
    std::size_t buffer_size_{};
    std::uint64_t total_bytes_{};
    bool finalized_{};
    std::array<std::uint8_t,32> digest_{};
};

std::string sha256(std::string_view data);
std::string hexLower(const std::uint8_t* data, std::size_t size);
bool parseHex32(std::string_view hex, std::array<std::uint8_t,32>& out);

} // namespace fasttris
