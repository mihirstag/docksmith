#include "cache_engine.h"

#include <sstream>
#include <vector>

#include "utils.h"

namespace docksmith {

std::string computeCacheKey(const CacheKeyInput& input) {
    std::ostringstream oss;
    oss << "prev=" << input.previousDigest << "\n";
    oss << "instruction=" << input.instructionText << "\n";
    oss << "workdir=" << input.workdir << "\n";
    oss << "env=" << serializeSortedEnv(input.env) << "\n";

    if (!input.copySourceHashes.empty()) {
        oss << "copy_sources=";
        for (const auto& [path, hash] : input.copySourceHashes) {
            oss << path << ':' << hash << '\n';
        }
    }

    const std::string payload = oss.str();
    return "sha256:" + sha256Bytes(std::vector<unsigned char>(payload.begin(), payload.end()));
}

} // namespace docksmith
