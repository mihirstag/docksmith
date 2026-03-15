#include "build_engine.h"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <map>
#include <nlohmann/json.hpp>
#include <optional>
#include <set>
#include <sstream>

#include "cache_engine.h"
#include "dockerfile_parser.h"
#include "errors.h"
#include "layer_engine.h"
#include "runtime_engine.h"
#include "utils.h"

namespace docksmith {

using json = nlohmann::json;
namespace fs = std::filesystem;

namespace {

struct BuildState {
    std::map<std::string, std::string> env;
    std::string workdir;
    std::vector<std::string> cmd;
    std::string previousLayerDigest;
};

struct CopySpec {
    std::string src;
    std::string dest;
};

ImageRef parseFromImageRef(const Instruction& instruction) {
    const std::string raw = trim(instruction.argText);
    if (raw.empty()) {
        throw DocksmithError("Invalid FROM at line " + std::to_string(instruction.lineNumber));
    }

    const auto colon = raw.find(':');
    if (colon == std::string::npos) {
        return ImageRef{raw, "latest"};
    }
    if (colon == 0 || colon == raw.size() - 1) {
        throw DocksmithError("Invalid FROM at line " + std::to_string(instruction.lineNumber) + ": " + raw);
    }
    return ImageRef{raw.substr(0, colon), raw.substr(colon + 1)};
}

std::string stripDigestPrefix(std::string digest) {
    if (digest.rfind("sha256:", 0) == 0) {
        digest = digest.substr(7);
    }
    return digest;
}

bool containsWildcard(const std::string& p) {
    return p.find('*') != std::string::npos || p.find('?') != std::string::npos;
}

std::vector<std::string> split(const std::string& input, char delimiter) {
    std::vector<std::string> out;
    std::string current;
    for (char ch : input) {
        if (ch == delimiter) {
            out.push_back(current);
            current.clear();
        } else {
            current.push_back(ch);
        }
    }
    out.push_back(current);
    return out;
}

bool matchComponent(const std::string& pattern, const std::string& text) {
    std::size_t p = 0;
    std::size_t t = 0;
    std::size_t star = std::string::npos;
    std::size_t match = 0;

    while (t < text.size()) {
        if (p < pattern.size() && (pattern[p] == '?' || pattern[p] == text[t])) {
            ++p;
            ++t;
        } else if (p < pattern.size() && pattern[p] == '*') {
            star = p++;
            match = t;
        } else if (star != std::string::npos) {
            p = star + 1;
            t = ++match;
        } else {
            return false;
        }
    }

    while (p < pattern.size() && pattern[p] == '*') {
        ++p;
    }

    return p == pattern.size();
}

bool matchPathRec(
    const std::vector<std::string>& patt,
    const std::vector<std::string>& path,
    std::size_t pi,
    std::size_t si) {
    if (pi == patt.size()) {
        return si == path.size();
    }

    if (patt[pi] == "**") {
        if (pi + 1 == patt.size()) {
            return true;
        }
        for (std::size_t k = si; k <= path.size(); ++k) {
            if (matchPathRec(patt, path, pi + 1, k)) {
                return true;
            }
        }
        return false;
    }

    if (si >= path.size()) {
        return false;
    }

    if (!matchComponent(patt[pi], path[si])) {
        return false;
    }

    return matchPathRec(patt, path, pi + 1, si + 1);
}

bool matchesGlob(const std::string& pattern, const std::string& relativePath) {
    const auto patt = split(normalizeUnixPath(pattern), '/');
    const auto path = split(normalizeUnixPath(relativePath), '/');
    return matchPathRec(patt, path, 0, 0);
}

CopySpec parseCopySpec(const Instruction& instruction) {
    std::istringstream iss(instruction.argText);
    std::string src;
    std::string dest;
    iss >> src >> dest;

    if (src.empty() || dest.empty()) {
        throw DocksmithError("Invalid COPY at line " + std::to_string(instruction.lineNumber) + ": expected COPY <src> <dest>");
    }

    std::string extra;
    iss >> extra;
    if (!extra.empty()) {
        throw DocksmithError("Invalid COPY at line " + std::to_string(instruction.lineNumber) + ": too many arguments");
    }

    return CopySpec{src, dest};
}

std::pair<std::string, std::string> parseEnv(const Instruction& instruction) {
    const auto pos = instruction.argText.find('=');
    if (pos == std::string::npos || pos == 0) {
        throw DocksmithError("Invalid ENV at line " + std::to_string(instruction.lineNumber) + ": expected ENV KEY=VALUE");
    }

    return {instruction.argText.substr(0, pos), instruction.argText.substr(pos + 1)};
}

std::vector<std::string> parseCmdJsonArray(const Instruction& instruction) {
    try {
        const json parsed = json::parse(instruction.argText);
        if (!parsed.is_array()) {
            throw DocksmithError("Invalid CMD at line " + std::to_string(instruction.lineNumber) + ": JSON array required");
        }

        std::vector<std::string> cmd;
        for (const auto& item : parsed) {
            if (!item.is_string()) {
                throw DocksmithError("Invalid CMD at line " + std::to_string(instruction.lineNumber) + ": all entries must be strings");
            }
            cmd.push_back(item.get<std::string>());
        }
        return cmd;
    } catch (const json::exception&) {
        throw DocksmithError("Invalid CMD at line " + std::to_string(instruction.lineNumber) + ": malformed JSON array");
    }
}

std::vector<fs::path> resolveCopySources(const fs::path& contextDir, const std::string& srcSpec) {
    std::vector<fs::path> resolved;

    if (!containsWildcard(srcSpec)) {
        const fs::path direct = contextDir / fs::path(srcSpec);
        if (!fs::exists(direct)) {
            throw DocksmithError("COPY source not found: " + srcSpec);
        }

        if (fs::is_directory(direct)) {
            for (auto it = fs::recursive_directory_iterator(direct); it != fs::recursive_directory_iterator(); ++it) {
                if (it->is_regular_file()) {
                    resolved.push_back(it->path());
                }
            }
        } else if (fs::is_regular_file(direct)) {
            resolved.push_back(direct);
        }

        std::sort(resolved.begin(), resolved.end());
        return resolved;
    }

    for (auto it = fs::recursive_directory_iterator(contextDir); it != fs::recursive_directory_iterator(); ++it) {
        if (!it->is_regular_file()) {
            continue;
        }

        const std::string rel = fs::relative(it->path(), contextDir).generic_string();
        if (matchesGlob(srcSpec, rel)) {
            resolved.push_back(it->path());
        }
    }

    std::sort(resolved.begin(), resolved.end());
    if (resolved.empty()) {
        throw DocksmithError("COPY glob matched no files: " + srcSpec);
    }
    return resolved;
}

std::string resolveDestBase(const std::string& dest, const BuildState& state) {
    if (!dest.empty() && dest.front() == '/') {
        return normalizeUnixPath(dest);
    }

    const std::string workdir = state.workdir.empty() ? "/" : state.workdir;
    if (workdir == "/") {
        return normalizeUnixPath("/" + dest);
    }
    return normalizeUnixPath(workdir + "/" + dest);
}

std::string makeDestPath(
    const fs::path& contextDir,
    const fs::path& source,
    const std::string& srcSpec,
    const std::string& destBase) {
    const fs::path srcSpecPath(srcSpec);
    const bool srcIsDir = !containsWildcard(srcSpec) && fs::is_directory(contextDir / srcSpecPath);
    if (srcIsDir) {
        const auto rel = fs::relative(source, contextDir / srcSpecPath).generic_string();
        return normalizeUnixPath(destBase + "/" + rel);
    }

    if (containsWildcard(srcSpec)) {
        const auto rel = fs::relative(source, contextDir).generic_string();
        return normalizeUnixPath(destBase + "/" + rel);
    }

    if (!destBase.empty() && destBase.back() == '/') {
        return normalizeUnixPath(destBase + source.filename().generic_string());
    }

    return normalizeUnixPath(destBase);
}

void ensureWorkdirExists(const fs::path& rootfs, const std::string& workdir) {
    if (workdir.empty()) {
        return;
    }

    std::string rel = normalizeUnixPath(workdir);
    if (rel.front() == '/') {
        rel = rel.substr(1);
    }
    if (!rel.empty()) {
        fs::create_directories(rootfs / rel);
    }
}

std::vector<std::pair<std::string, std::string>> computeCopyInputHashes(
    const fs::path& contextDir,
    const std::string& srcSpec) {
    std::vector<std::pair<std::string, std::string>> hashes;
    const auto files = resolveCopySources(contextDir, srcSpec);

    for (const auto& file : files) {
        const std::string rel = fs::relative(file, contextDir).generic_string();
        hashes.push_back({rel, "sha256:" + sha256File(file)});
    }

    std::sort(hashes.begin(), hashes.end(), [](const auto& a, const auto& b) {
        return a.first < b.first;
    });

    return hashes;
}

void applyCopy(const fs::path& contextDir, const fs::path& rootfs, const CopySpec& spec, const BuildState& state) {
    const auto sources = resolveCopySources(contextDir, spec.src);
    const std::string destBase = resolveDestBase(spec.dest, state);

    for (const auto& source : sources) {
        const std::string dest = makeDestPath(contextDir, source, spec.src, destBase);
        std::string rel = dest;
        if (!rel.empty() && rel.front() == '/') {
            rel = rel.substr(1);
        }

        const fs::path target = rootfs / fs::path(rel);
        fs::create_directories(target.parent_path());
        fs::copy_file(source, target, fs::copy_options::overwrite_existing);
    }
}

ImageManifest assembleManifest(
    const ImageRef& target,
    const std::string& created,
    const BuildState& state,
    const std::vector<Layer>& layers) {
    ImageManifest manifest;
    manifest.name = target.name;
    manifest.tag = target.tag;
    manifest.digest = "";
    manifest.created = created;

    for (const auto& [k, v] : state.env) {
        manifest.config.Env.push_back(k + "=" + v);
    }

    manifest.config.Cmd = state.cmd;
    manifest.config.WorkingDir = state.workdir;
    manifest.layers = layers;

    json j;
    j["name"] = manifest.name;
    j["tag"] = manifest.tag;
    j["digest"] = "";
    j["created"] = manifest.created;
    j["config"] = {
        {"Env", manifest.config.Env},
        {"Cmd", manifest.config.Cmd},
        {"WorkingDir", manifest.config.WorkingDir},
    };
    j["layers"] = json::array();

    for (const auto& layer : manifest.layers) {
        j["layers"].push_back(
            {
                {"digest", layer.digest},
                {"size", layer.size},
                {"createdBy", layer.createdBy},
            });
    }

    const std::string canonical = j.dump();
    manifest.digest = "sha256:" + sha256Bytes(std::vector<unsigned char>(canonical.begin(), canonical.end()));

    return manifest;
}

void extractBaseLayers(const StateStore& store, const ImageManifest& base, const fs::path& rootfs) {
    for (const auto& layer : base.layers) {
        const auto layerPath = store.layersDir() / (stripDigestPrefix(layer.digest) + ".tar");
        if (!fs::exists(layerPath)) {
            throw DocksmithError("Base layer missing on disk: " + layerPath.string());
        }
        extractLayerTar(layerPath, rootfs);
    }
}

} // namespace

BuildCommandOptions parseBuildOptions(const std::vector<std::string>& args) {
    BuildCommandOptions options;

    for (std::size_t i = 0; i < args.size(); ++i) {
        const auto& arg = args[i];
        if (arg == "-t") {
            if (i + 1 >= args.size()) {
                throw DocksmithError("build -t requires <name:tag>");
            }
            options.target = parseImageRef(args[++i]);
            continue;
        }

        if (arg == "--no-cache") {
            options.noCache = true;
            continue;
        }

        if (arg.rfind("-", 0) == 0) {
            throw DocksmithError("Unknown build flag: " + arg);
        }

        if (!options.contextDir.empty()) {
            throw DocksmithError("build expects exactly one <context> directory");
        }
        options.contextDir = arg;
    }

    if (options.target.name.empty() || options.target.tag.empty()) {
        throw DocksmithError("build requires -t <name:tag>");
    }

    if (options.contextDir.empty()) {
        throw DocksmithError("build requires <context>");
    }

    return options;
}

BuildResult buildImage(const StateStore& store, const BuildCommandOptions& options) {
    const auto start = std::chrono::steady_clock::now();

    const fs::path contextDir = fs::absolute(fs::path(options.contextDir));
    if (!fs::exists(contextDir) || !fs::is_directory(contextDir)) {
        throw DocksmithError("Build context not found: " + contextDir.string());
    }

    const fs::path docksmithfile = contextDir / "Docksmithfile";
    const ParsedDocksmithfile parsed = parseDocksmithfile(docksmithfile);

    if (parsed.instructions.front().type != InstructionType::From) {
        throw DocksmithError("First instruction must be FROM");
    }

    const Instruction& fromInst = parsed.instructions.front();
    const ImageRef baseRef = parseFromImageRef(fromInst);
    const auto baseManifestOpt = store.findImage(baseRef);
    if (!baseManifestOpt.has_value()) {
        throw DocksmithError("Base image not found locally: " + imageRefToString(baseRef));
    }

    const ImageManifest& baseManifest = baseManifestOpt.value();

    fs::path buildRoot = fs::temp_directory_path() / fs::path("docksmith-build-root-" + std::to_string(std::rand()));
    fs::create_directories(buildRoot);
    extractBaseLayers(store, baseManifest, buildRoot);

    BuildState state;
    state.previousLayerDigest = baseManifest.digest;

    std::vector<Layer> layers = baseManifest.layers;
    bool cascadeMiss = options.noCache;
    bool allLayerStepsHit = !options.noCache;

    const auto existingManifest = store.findImage(options.target);
    const std::string defaultCreated = getCurrentUtcIso8601();

    int stepIndex = 0;
    for (const auto& instruction : parsed.instructions) {
        ++stepIndex;
        std::cout << "Step " << stepIndex << "/" << parsed.instructions.size() << " : " << instruction.rawText;

        if (instruction.type == InstructionType::From) {
            std::cout << "\n";
            continue;
        }

        if (instruction.type == InstructionType::Workdir) {
            state.workdir = normalizeUnixPath(instruction.argText);
            if (state.workdir.empty() || state.workdir.front() != '/') {
                state.workdir = "/" + state.workdir;
            }
            std::cout << "\n";
            continue;
        }

        if (instruction.type == InstructionType::Env) {
            auto [k, v] = parseEnv(instruction);
            state.env[k] = v;
            std::cout << "\n";
            continue;
        }

        if (instruction.type == InstructionType::Cmd) {
            state.cmd = parseCmdJsonArray(instruction);
            std::cout << "\n";
            continue;
        }

        ensureWorkdirExists(buildRoot, state.workdir);

        const auto stepStart = std::chrono::steady_clock::now();
        std::optional<Layer> resultingLayer;

        std::string cacheKey;
        if (!options.noCache && !cascadeMiss) {
            CacheKeyInput keyInput;
            keyInput.previousDigest = state.previousLayerDigest;
            keyInput.instructionText = instruction.rawText;
            keyInput.workdir = state.workdir;
            keyInput.env = state.env;

            if (instruction.type == InstructionType::Copy) {
                const auto spec = parseCopySpec(instruction);
                keyInput.copySourceHashes = computeCopyInputHashes(contextDir, spec.src);
            }

            cacheKey = computeCacheKey(keyInput);

            const auto cachedDigest = store.lookupCache(cacheKey);
            if (cachedDigest.has_value()) {
                const std::string digestHex = stripDigestPrefix(cachedDigest.value());
                const fs::path layerPath = store.layersDir() / (digestHex + ".tar");
                if (fs::exists(layerPath)) {
                    extractLayerTar(layerPath, buildRoot);
                    Layer cached;
                    cached.digest = "sha256:" + digestHex;
                    cached.size = fs::file_size(layerPath);
                    cached.createdBy = instruction.rawText;
                    resultingLayer = cached;
                }
            }
        }

        if (resultingLayer.has_value()) {
            layers.push_back(resultingLayer.value());
            state.previousLayerDigest = resultingLayer->digest;
            const auto end = std::chrono::steady_clock::now();
            const double seconds = std::chrono::duration<double>(end - stepStart).count();
            std::cout << " [CACHE HIT] " << std::fixed << std::setprecision(2) << seconds << "s\n";
            continue;
        }

        allLayerStepsHit = false;
        cascadeMiss = true;

        const auto before = snapshotTree(buildRoot);

        if (instruction.type == InstructionType::Copy) {
            const auto spec = parseCopySpec(instruction);
            applyCopy(contextDir, buildRoot, spec, state);
        } else if (instruction.type == InstructionType::Run) {
            const std::string cmd = instruction.argText;
            std::vector<std::string> argv = {"/bin/sh", "-c", cmd};
            const int rc = executeIsolated(buildRoot, argv, state.env, state.workdir.empty() ? "/" : state.workdir);
            if (rc != 0) {
                throw DocksmithError("RUN failed at line " + std::to_string(instruction.lineNumber) + " with exit code " + std::to_string(rc));
            }
        }

        const auto after = snapshotTree(buildRoot);
        const auto changedFiles = computeChangedFiles(before, after);
        const auto newDirs = computeNewDirectories(before, after);

        Layer layer = createDeterministicLayerFromChanges(
            buildRoot,
            changedFiles,
            newDirs,
            instruction.rawText,
            store.layersDir());

        if (!options.noCache && !cacheKey.empty()) {
            store.storeCache(cacheKey, layer.digest);
        }

        layers.push_back(layer);
        state.previousLayerDigest = layer.digest;

        const auto end = std::chrono::steady_clock::now();
        const double seconds = std::chrono::duration<double>(end - stepStart).count();
        std::cout << " [CACHE MISS] " << std::fixed << std::setprecision(2) << seconds << "s\n";
    }

    const std::string created =
        (allLayerStepsHit && existingManifest.has_value())
            ? existingManifest->created
            : defaultCreated;

    ImageManifest manifest = assembleManifest(options.target, created, state, layers);
    store.writeManifest(manifest);

    fs::remove_all(buildRoot);

    const auto end = std::chrono::steady_clock::now();
    BuildResult result;
    result.manifest = manifest;
    result.allLayerStepsCacheHit = allLayerStepsHit;
    result.totalSeconds = std::chrono::duration<double>(end - start).count();
    return result;
}

void runBuildCommand(const StateStore& store, const std::vector<std::string>& args) {
    const auto options = parseBuildOptions(args);
    const auto result = buildImage(store, options);

    std::cout << "Successfully built " << result.manifest.digest.substr(0, 19)
              << "  " << imageRefToString(options.target) << " ("
              << std::fixed << std::setprecision(2) << result.totalSeconds << "s)\n";
}

void runImagesCommand(const StateStore& store) {
    auto images = store.listImages();

    std::sort(images.begin(), images.end(), [](const auto& a, const auto& b) {
        if (a.name == b.name) {
            return a.tag < b.tag;
        }
        return a.name < b.name;
    });

    std::cout << std::left << std::setw(20) << "NAME"
              << std::setw(15) << "TAG"
              << std::setw(15) << "ID"
              << "CREATED\n";

    for (const auto& image : images) {
        std::string id = image.digest;
        if (id.rfind("sha256:", 0) == 0) {
            id = id.substr(7);
        }
        if (id.size() > 12) {
            id = id.substr(0, 12);
        }

        std::cout << std::left << std::setw(20) << image.name
                  << std::setw(15) << image.tag
                  << std::setw(15) << id
                  << image.created << "\n";
    }
}

void runRmiCommand(const StateStore& store, const std::vector<std::string>& args) {
    if (args.size() != 1) {
        throw DocksmithError("docksmith rmi requires exactly one <name:tag>");
    }

    const auto image = parseImageRef(args[0]);
    store.deleteImageAndLayers(image);
    std::cout << "Untagged and removed image: " << imageRefToString(image) << "\n";
}

} // namespace docksmith
