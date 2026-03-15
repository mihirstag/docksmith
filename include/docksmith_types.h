#pragma once

#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace docksmith {

struct ImageConfig {
    std::vector<std::string> Env;
    std::vector<std::string> Cmd;
    std::string WorkingDir;
};

struct Layer {
    std::string digest;
    std::uint64_t size{};
    std::string createdBy;
};

struct ImageManifest {
    std::string name;
    std::string tag;
    std::string digest;
    std::string created;
    ImageConfig config;
    std::vector<Layer> layers;
};

struct ImageRef {
    std::string name;
    std::string tag;
};

struct BuildCommandOptions {
    ImageRef target;
    std::string contextDir;
    bool noCache{false};
};

struct BuildResult {
    ImageManifest manifest;
    bool allLayerStepsCacheHit{true};
    double totalSeconds{0.0};
};

struct RunCommandOptions {
    ImageRef image;
    std::vector<std::string> commandOverride;
    std::map<std::string, std::string> envOverrides;
};

} // namespace docksmith
