#include "layer_engine.h"

#include <algorithm>
#include <cstdlib>
#include <fstream>
#include <set>

#include "errors.h"
#include "utils.h"

namespace docksmith {
namespace {
std::string toUnixRelative(const fs::path& root, const fs::path& value) {
    auto rel = fs::relative(value, root).generic_string();
    if (rel == ".") {
        return "";
    }
    return rel;
}

void runSystemOrThrow(const std::string& command, const std::string& errorContext) {
    const int rc = std::system(command.c_str());
    if (rc != 0) {
        throw DocksmithError(errorContext + " (exit code " + std::to_string(rc) + ")");
    }
}
} // namespace

FileSnapshot snapshotTree(const fs::path& root) {
    FileSnapshot snapshot;

    if (!fs::exists(root)) {
        return snapshot;
    }

    for (auto it = fs::recursive_directory_iterator(root); it != fs::recursive_directory_iterator(); ++it) {
        const auto& path = it->path();
        const std::string rel = toUnixRelative(root, path);
        if (rel.empty()) {
            continue;
        }

        if (it->is_directory()) {
            snapshot.directories.push_back(rel);
            continue;
        }

        if (it->is_regular_file()) {
            snapshot.fileHashes[rel] = "sha256:" + sha256File(path);
        }
    }

    std::sort(snapshot.directories.begin(), snapshot.directories.end());
    return snapshot;
}

std::vector<std::string> computeChangedFiles(
    const FileSnapshot& before,
    const FileSnapshot& after) {
    std::vector<std::string> changed;
    for (const auto& [path, hash] : after.fileHashes) {
        auto it = before.fileHashes.find(path);
        if (it == before.fileHashes.end() || it->second != hash) {
            changed.push_back(path);
        }
    }
    return changed;
}

std::vector<std::string> computeNewDirectories(
    const FileSnapshot& before,
    const FileSnapshot& after) {
    std::set<std::string> beforeSet(before.directories.begin(), before.directories.end());
    std::vector<std::string> newDirs;

    for (const auto& dir : after.directories) {
        if (beforeSet.find(dir) == beforeSet.end()) {
            newDirs.push_back(dir);
        }
    }
    return newDirs;
}

Layer createDeterministicLayerFromChanges(
    const fs::path& root,
    const std::vector<std::string>& changedFiles,
    const std::vector<std::string>& newDirs,
    const std::string& createdBy,
    const fs::path& layerStoreDir) {
    const fs::path stagingRoot = fs::temp_directory_path() / fs::path("docksmith-layer-staging-" + std::to_string(std::rand()));
    fs::create_directories(stagingRoot);

    for (const auto& dirRel : newDirs) {
        fs::create_directories(stagingRoot / fs::path(dirRel));
    }

    for (const auto& rel : changedFiles) {
        const fs::path source = root / fs::path(rel);
        if (!fs::exists(source) || !fs::is_regular_file(source)) {
            continue;
        }

        const fs::path target = stagingRoot / fs::path(rel);
        fs::create_directories(target.parent_path());
        fs::copy_file(source, target, fs::copy_options::overwrite_existing);
    }

    std::vector<std::string> entries;
    for (const auto& dir : newDirs) {
        entries.push_back(dir);
    }
    for (const auto& file : changedFiles) {
        if (fs::exists(stagingRoot / fs::path(file))) {
            entries.push_back(file);
        }
    }
    std::sort(entries.begin(), entries.end());

    const fs::path tarOutput = fs::temp_directory_path() / fs::path("docksmith-layer-" + std::to_string(std::rand()) + ".tar");
    std::string cmd;
#if defined(_WIN32)
    throw DocksmithError("Layer creation requires Linux tar command");
#else
    if (entries.empty()) {
        cmd = "bash -lc \"tar --sort=name --mtime='@0' --owner=0 --group=0 --numeric-owner -cf '" + tarOutput.string() + "' --files-from /dev/null\"";
    } else {
        const fs::path listFile = fs::temp_directory_path() / fs::path("docksmith-layer-files-" + std::to_string(std::rand()) + ".txt");
        {
            std::ofstream out(listFile);
            for (const auto& e : entries) {
                out << e << '\n';
            }
        }

        cmd = "bash -lc \"cd '" + stagingRoot.string() + "' && tar --sort=name --mtime='@0' --owner=0 --group=0 --numeric-owner -cf '" + tarOutput.string() + "' -T '" + listFile.string() + "'\"";
        runSystemOrThrow(cmd, "Failed to create deterministic layer tar");
        fs::remove(listFile);
    }

    if (entries.empty()) {
        runSystemOrThrow(cmd, "Failed to create empty deterministic layer tar");
    }
#endif

    const std::string digestHex = sha256File(tarOutput);
    const std::string digest = "sha256:" + digestHex;
    const fs::path finalPath = layerStoreDir / (digestHex + ".tar");

    if (!fs::exists(finalPath)) {
        fs::create_directories(layerStoreDir);
        fs::copy_file(tarOutput, finalPath, fs::copy_options::overwrite_existing);
    }

    Layer layer;
    layer.digest = digest;
    layer.size = fs::file_size(finalPath);
    layer.createdBy = createdBy;

    fs::remove_all(stagingRoot);
    fs::remove(tarOutput);

    return layer;
}

void extractLayerTar(const fs::path& tarPath, const fs::path& destinationRoot) {
    fs::create_directories(destinationRoot);

#if defined(_WIN32)
    throw DocksmithError("Layer extraction requires Linux tar command");
#else
    const std::string command = "bash -lc \"tar -xf '" + tarPath.string() + "' -C '" + destinationRoot.string() + "'\"";
    const int rc = std::system(command.c_str());
    if (rc != 0) {
        throw DocksmithError("Failed to extract layer: " + tarPath.string());
    }
#endif
}

} // namespace docksmith
