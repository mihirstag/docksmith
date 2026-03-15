#include "state_store.h"

#include <cstdlib>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <nlohmann/json.hpp>

#include "errors.h"

namespace docksmith {

using json = nlohmann::json;

namespace {
std::string getHomeDir() {
#if defined(_WIN32)
    const char* userProfile = std::getenv("USERPROFILE");
    if (userProfile && std::string(userProfile).size() > 0) {
        return std::string(userProfile);
    }
#endif
    const char* home = std::getenv("HOME");
    if (!home || std::string(home).empty()) {
        throw DocksmithError("Unable to resolve home directory (HOME/USERPROFILE not set)");
    }
    return std::string(home);
}

json manifestToJson(const ImageManifest& manifest) {
    json j;
    j["name"] = manifest.name;
    j["tag"] = manifest.tag;
    j["digest"] = manifest.digest;
    j["created"] = manifest.created;
    j["config"] = {
        {"Env", manifest.config.Env},
        {"Cmd", manifest.config.Cmd},
        {"WorkingDir", manifest.config.WorkingDir}
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

    return j;
}

ImageManifest jsonToManifest(const json& j) {
    ImageManifest manifest;
    manifest.name = j.value("name", "");
    manifest.tag = j.value("tag", "");
    manifest.digest = j.value("digest", "");
    manifest.created = j.value("created", "");

    if (j.contains("config") && j["config"].is_object()) {
        manifest.config.Env = j["config"].value("Env", std::vector<std::string>{});
        manifest.config.Cmd = j["config"].value("Cmd", std::vector<std::string>{});
        manifest.config.WorkingDir = j["config"].value("WorkingDir", "");
    }

    if (j.contains("layers") && j["layers"].is_array()) {
        for (const auto& layerJ : j["layers"]) {
            Layer layer;
            layer.digest = layerJ.value("digest", "");
            layer.size = layerJ.value("size", static_cast<std::uint64_t>(0));
            layer.createdBy = layerJ.value("createdBy", "");
            manifest.layers.push_back(std::move(layer));
        }
    }

    return manifest;
}
} // namespace

StateStore::StateStore() : basePath_(fs::path(getHomeDir()) / ".docksmith") {}

void StateStore::initializeState() const {
    fs::create_directories(baseDir());
    fs::create_directories(imagesDir());
    fs::create_directories(layersDir());
    fs::create_directories(cacheDir());

    if (!fs::exists(cacheIndexPath())) {
        std::ofstream file(cacheIndexPath());
        file << "{}";
    }
}

const fs::path& StateStore::baseDir() const { return basePath_; }

fs::path StateStore::imagesDir() const { return basePath_ / "images"; }

fs::path StateStore::layersDir() const { return basePath_ / "layers"; }

fs::path StateStore::cacheDir() const { return basePath_ / "cache"; }

fs::path StateStore::cacheIndexPath() const { return cacheDir() / "index.json"; }

fs::path StateStore::manifestPathFor(const ImageRef& imageRef) const {
    return imagesDir() / (imageRef.name + "_" + imageRef.tag + ".json");
}

std::optional<ImageManifest> StateStore::findImage(const ImageRef& imageRef) const {
    for (const auto& entry : fs::directory_iterator(imagesDir())) {
        if (!entry.is_regular_file() || entry.path().extension() != ".json") {
            continue;
        }

        std::ifstream file(entry.path());
        if (!file) {
            continue;
        }

        try {
            json j;
            file >> j;
            const auto name = j.value("name", "");
            const auto tag = j.value("tag", "");
            if (name == imageRef.name && tag == imageRef.tag) {
                return jsonToManifest(j);
            }
        } catch (...) {
            continue;
        }
    }

    return std::nullopt;
}

std::vector<ImageManifest> StateStore::listImages() const {
    std::vector<ImageManifest> manifests;

    for (const auto& entry : fs::directory_iterator(imagesDir())) {
        if (!entry.is_regular_file() || entry.path().extension() != ".json") {
            continue;
        }

        std::ifstream file(entry.path());
        if (!file) {
            continue;
        }

        try {
            json j;
            file >> j;
            manifests.push_back(jsonToManifest(j));
        } catch (...) {
            continue;
        }
    }

    return manifests;
}

void StateStore::writeManifest(const ImageManifest& manifest) const {
    json j = manifestToJson(manifest);
    const fs::path path = manifestPathFor(ImageRef{manifest.name, manifest.tag});
    std::ofstream file(path);
    if (!file) {
        throw DocksmithError("Unable to write manifest: " + path.string());
    }
    file << std::setw(2) << j;
}

void StateStore::deleteImageAndLayers(const ImageRef& imageRef) const {
    const auto manifestOpt = findImage(imageRef);
    if (!manifestOpt.has_value()) {
        throw DocksmithError("No such image: " + imageRefToString(imageRef));
    }

    const auto& manifest = manifestOpt.value();
    for (const auto& layer : manifest.layers) {
        std::string digest = layer.digest;
        if (digest.rfind("sha256:", 0) == 0) {
            digest = digest.substr(7);
        }

        const fs::path layerPath = layersDir() / (digest + ".tar");
        if (fs::exists(layerPath)) {
            fs::remove(layerPath);
        }
    }

    fs::remove(manifestPathFor(imageRef));
}

std::optional<std::string> StateStore::lookupCache(const std::string& cacheKey) const {
    std::ifstream file(cacheIndexPath());
    if (!file) {
        return std::nullopt;
    }

    json cache;
    file >> cache;

    if (!cache.is_object() || !cache.contains(cacheKey)) {
        return std::nullopt;
    }

    return cache[cacheKey].get<std::string>();
}

void StateStore::storeCache(const std::string& cacheKey, const std::string& layerDigest) const {
    json cache = json::object();

    if (fs::exists(cacheIndexPath())) {
        std::ifstream file(cacheIndexPath());
        if (file) {
            try {
                file >> cache;
                if (!cache.is_object()) {
                    cache = json::object();
                }
            } catch (...) {
                cache = json::object();
            }
        }
    }

    cache[cacheKey] = layerDigest;

    std::ofstream out(cacheIndexPath());
    if (!out) {
        throw DocksmithError("Unable to write cache index: " + cacheIndexPath().string());
    }
    out << std::setw(2) << cache;
}

ImageRef parseImageRef(const std::string& raw) {
    const auto colon = raw.find(':');
    if (colon == std::string::npos || colon == 0 || colon == raw.size() - 1) {
        throw DocksmithError("Image reference must be <name:tag>: " + raw);
    }

    ImageRef ref;
    ref.name = raw.substr(0, colon);
    ref.tag = raw.substr(colon + 1);
    return ref;
}

std::string imageRefToString(const ImageRef& image) {
    return image.name + ":" + image.tag;
}

} // namespace docksmith
