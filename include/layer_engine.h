#pragma once

#include <filesystem>
#include <map>
#include <string>
#include <vector>

#include "docksmith_types.h"

namespace docksmith {

namespace fs = std::filesystem;

struct FileSnapshot {
    std::map<std::string, std::string> fileHashes;
    std::vector<std::string> directories;
};

FileSnapshot snapshotTree(const fs::path& root);

std::vector<std::string> computeChangedFiles(
    const FileSnapshot& before,
    const FileSnapshot& after);

std::vector<std::string> computeNewDirectories(
    const FileSnapshot& before,
    const FileSnapshot& after);

Layer createDeterministicLayerFromChanges(
    const fs::path& root,
    const std::vector<std::string>& changedFiles,
    const std::vector<std::string>& newDirs,
    const std::string& createdBy,
    const fs::path& layerStoreDir);

void extractLayerTar(const fs::path& tarPath, const fs::path& destinationRoot);

} // namespace docksmith
