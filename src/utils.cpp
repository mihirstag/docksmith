#include "utils.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <chrono>
#include <cstdint>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <stdexcept>

namespace docksmith {
namespace {
class Sha256 {
public:
    Sha256() { reset(); }

    void reset() {
        dataLen_ = 0;
        bitLen_ = 0;
        state_[0] = 0x6a09e667;
        state_[1] = 0xbb67ae85;
        state_[2] = 0x3c6ef372;
        state_[3] = 0xa54ff53a;
        state_[4] = 0x510e527f;
        state_[5] = 0x9b05688c;
        state_[6] = 0x1f83d9ab;
        state_[7] = 0x5be0cd19;
    }

    void update(const unsigned char* data, std::size_t len) {
        for (std::size_t i = 0; i < len; ++i) {
            data_[dataLen_] = data[i];
            dataLen_++;
            if (dataLen_ == 64) {
                transform();
                bitLen_ += 512;
                dataLen_ = 0;
            }
        }
    }

    std::array<unsigned char, 32> final() {
        std::array<unsigned char, 32> hash{};

        std::size_t i = dataLen_;
        if (dataLen_ < 56) {
            data_[i++] = 0x80;
            while (i < 56) {
                data_[i++] = 0x00;
            }
        } else {
            data_[i++] = 0x80;
            while (i < 64) {
                data_[i++] = 0x00;
            }
            transform();
            std::fill(data_.begin(), data_.begin() + 56, 0x00);
        }

        bitLen_ += static_cast<std::uint64_t>(dataLen_) * 8;
        data_[63] = static_cast<unsigned char>(bitLen_);
        data_[62] = static_cast<unsigned char>(bitLen_ >> 8);
        data_[61] = static_cast<unsigned char>(bitLen_ >> 16);
        data_[60] = static_cast<unsigned char>(bitLen_ >> 24);
        data_[59] = static_cast<unsigned char>(bitLen_ >> 32);
        data_[58] = static_cast<unsigned char>(bitLen_ >> 40);
        data_[57] = static_cast<unsigned char>(bitLen_ >> 48);
        data_[56] = static_cast<unsigned char>(bitLen_ >> 56);
        transform();

        for (i = 0; i < 4; ++i) {
            hash[i] = static_cast<unsigned char>((state_[0] >> (24 - i * 8)) & 0x000000ff);
            hash[i + 4] = static_cast<unsigned char>((state_[1] >> (24 - i * 8)) & 0x000000ff);
            hash[i + 8] = static_cast<unsigned char>((state_[2] >> (24 - i * 8)) & 0x000000ff);
            hash[i + 12] = static_cast<unsigned char>((state_[3] >> (24 - i * 8)) & 0x000000ff);
            hash[i + 16] = static_cast<unsigned char>((state_[4] >> (24 - i * 8)) & 0x000000ff);
            hash[i + 20] = static_cast<unsigned char>((state_[5] >> (24 - i * 8)) & 0x000000ff);
            hash[i + 24] = static_cast<unsigned char>((state_[6] >> (24 - i * 8)) & 0x000000ff);
            hash[i + 28] = static_cast<unsigned char>((state_[7] >> (24 - i * 8)) & 0x000000ff);
        }

        return hash;
    }

private:
    std::array<unsigned char, 64> data_{};
    std::uint32_t dataLen_{};
    std::uint64_t bitLen_{};
    std::array<std::uint32_t, 8> state_{};

    static constexpr std::array<std::uint32_t, 64> k_ = {
        0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5,
        0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
        0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3,
        0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
        0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc,
        0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
        0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7,
        0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
        0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13,
        0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
        0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3,
        0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
        0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5,
        0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
        0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208,
        0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2};

    static std::uint32_t rotr(std::uint32_t x, std::uint32_t n) {
        return (x >> n) | (x << (32 - n));
    }

    void transform() {
        std::array<std::uint32_t, 64> m{};

        for (std::size_t i = 0, j = 0; i < 16; ++i, j += 4) {
            m[i] = (static_cast<std::uint32_t>(data_[j]) << 24) |
                   (static_cast<std::uint32_t>(data_[j + 1]) << 16) |
                   (static_cast<std::uint32_t>(data_[j + 2]) << 8) |
                   (static_cast<std::uint32_t>(data_[j + 3]));
        }

        for (std::size_t i = 16; i < 64; ++i) {
            const std::uint32_t s0 = rotr(m[i - 15], 7) ^ rotr(m[i - 15], 18) ^ (m[i - 15] >> 3);
            const std::uint32_t s1 = rotr(m[i - 2], 17) ^ rotr(m[i - 2], 19) ^ (m[i - 2] >> 10);
            m[i] = m[i - 16] + s0 + m[i - 7] + s1;
        }

        std::uint32_t a = state_[0];
        std::uint32_t b = state_[1];
        std::uint32_t c = state_[2];
        std::uint32_t d = state_[3];
        std::uint32_t e = state_[4];
        std::uint32_t f = state_[5];
        std::uint32_t g = state_[6];
        std::uint32_t h = state_[7];

        for (std::size_t i = 0; i < 64; ++i) {
            const std::uint32_t s1 = rotr(e, 6) ^ rotr(e, 11) ^ rotr(e, 25);
            const std::uint32_t ch = (e & f) ^ ((~e) & g);
            const std::uint32_t temp1 = h + s1 + ch + k_[i] + m[i];
            const std::uint32_t s0 = rotr(a, 2) ^ rotr(a, 13) ^ rotr(a, 22);
            const std::uint32_t maj = (a & b) ^ (a & c) ^ (b & c);
            const std::uint32_t temp2 = s0 + maj;

            h = g;
            g = f;
            f = e;
            e = d + temp1;
            d = c;
            c = b;
            b = a;
            a = temp1 + temp2;
        }

        state_[0] += a;
        state_[1] += b;
        state_[2] += c;
        state_[3] += d;
        state_[4] += e;
        state_[5] += f;
        state_[6] += g;
        state_[7] += h;
    }
};

std::string bytesToHex(const std::array<unsigned char, 32>& bytes) {
    std::ostringstream oss;
    oss << std::hex << std::setfill('0');
    for (const unsigned char b : bytes) {
        oss << std::setw(2) << static_cast<int>(b);
    }
    return oss.str();
}
} // namespace

std::string getCurrentUtcIso8601() {
    const auto now = std::chrono::system_clock::now();
    const std::time_t nowT = std::chrono::system_clock::to_time_t(now);

#if defined(_WIN32)
    std::tm tmUtc{};
    gmtime_s(&tmUtc, &nowT);
#else
    std::tm tmUtc{};
    gmtime_r(&nowT, &tmUtc);
#endif

    std::ostringstream oss;
    oss << std::put_time(&tmUtc, "%Y-%m-%dT%H:%M:%SZ");
    return oss.str();
}

std::vector<unsigned char> readBinaryFile(const fs::path& filePath) {
    std::ifstream file(filePath, std::ios::binary);
    if (!file) {
        throw std::runtime_error("Unable to read file: " + filePath.string());
    }
    return std::vector<unsigned char>(
        std::istreambuf_iterator<char>(file),
        std::istreambuf_iterator<char>());
}

std::string sha256Bytes(const std::vector<unsigned char>& bytes) {
    Sha256 sha;
    if (!bytes.empty()) {
        sha.update(bytes.data(), bytes.size());
    }
    return bytesToHex(sha.final());
}

std::string sha256File(const fs::path& filePath) {
    std::ifstream file(filePath, std::ios::binary);
    if (!file) {
        throw std::runtime_error("Unable to hash file: " + filePath.string());
    }

    Sha256 sha;
    std::array<char, 8192> buffer{};
    while (file.good()) {
        file.read(buffer.data(), static_cast<std::streamsize>(buffer.size()));
        const std::streamsize count = file.gcount();
        if (count > 0) {
            sha.update(reinterpret_cast<const unsigned char*>(buffer.data()), static_cast<std::size_t>(count));
        }
    }

    return bytesToHex(sha.final());
}

void writeTextFile(const fs::path& filePath, const std::string& content) {
    fs::create_directories(filePath.parent_path());
    std::ofstream file(filePath, std::ios::binary);
    if (!file) {
        throw std::runtime_error("Unable to write file: " + filePath.string());
    }
    file << content;
}

std::string readTextFile(const fs::path& filePath) {
    std::ifstream file(filePath, std::ios::binary);
    if (!file) {
        throw std::runtime_error("Unable to read file: " + filePath.string());
    }
    return std::string((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
}

std::string trim(const std::string& input) {
    const auto first = std::find_if_not(input.begin(), input.end(), [](unsigned char c) { return std::isspace(c); });
    if (first == input.end()) {
        return "";
    }
    const auto last = std::find_if_not(input.rbegin(), input.rend(), [](unsigned char c) { return std::isspace(c); }).base();
    return std::string(first, last);
}

bool startsWith(const std::string& value, const std::string& prefix) {
    return value.rfind(prefix, 0) == 0;
}

std::string joinCommand(const std::vector<std::string>& command) {
    std::ostringstream oss;
    for (std::size_t i = 0; i < command.size(); ++i) {
        if (i > 0) {
            oss << ' ';
        }
        oss << command[i];
    }
    return oss.str();
}

std::string shellEscapeSingleQuotes(const std::string& input) {
    std::string out;
    out.reserve(input.size() + 8);
    for (char ch : input) {
        if (ch == '\'') {
            out += "'\\''";
        } else {
            out.push_back(ch);
        }
    }
    return out;
}

std::string normalizeUnixPath(const std::string& path) {
    if (path.empty()) {
        return "";
    }

    std::string result = path;
    std::replace(result.begin(), result.end(), '\\', '/');

    std::string normalized;
    bool lastSlash = false;
    for (char ch : result) {
        if (ch == '/') {
            if (!lastSlash) {
                normalized.push_back(ch);
            }
            lastSlash = true;
        } else {
            lastSlash = false;
            normalized.push_back(ch);
        }
    }

    if (normalized.size() > 1 && normalized.back() == '/') {
        normalized.pop_back();
    }
    return normalized;
}

std::string toLower(const std::string& value) {
    std::string out = value;
    std::transform(out.begin(), out.end(), out.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return out;
}

std::string serializeSortedEnv(const std::map<std::string, std::string>& env) {
    std::ostringstream oss;
    bool first = true;
    for (const auto& [key, value] : env) {
        if (!first) {
            oss << '\n';
        }
        first = false;
        oss << key << '=' << value;
    }
    return oss.str();
}

} // namespace docksmith
