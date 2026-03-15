#pragma once

#include <map>
#include <string>
#include <vector>

namespace docksmith {

struct CacheKeyInput {
    std::string previousDigest;
    std::string instructionText;
    std::string workdir;
    std::map<std::string, std::string> env;
    std::vector<std::pair<std::string, std::string>> copySourceHashes;
};

std::string computeCacheKey(const CacheKeyInput& input);

} // namespace docksmith
