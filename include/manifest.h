#pragma once
#include <string>
#include <vector>

// Represents the "config" block in the manifest
struct ImageConfig {
    std::vector<std::string> Env;
    std::vector<std::string> Cmd;
    std::string WorkingDir;
};

// Represents a single layer entry in the manifest
struct Layer {
    std::string digest;
    size_t size;
    std::string createdBy;
};

// The main manifest structure that maps directly to the JSON file
struct ImageManifest {
    std::string name;
    std::string tag;
    std::string digest;
    std::string created;
    ImageConfig config;
    std::vector<Layer> layers;
};