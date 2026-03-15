#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

#include "docksmith_types.h"

namespace docksmith {

namespace fs = std::filesystem;

class StateStore {
public:
    StateStore();

    void initializeState() const;

    const fs::path& baseDir() const;
    fs::path imagesDir() const;
    fs::path layersDir() const;
    fs::path cacheDir() const;

    std::optional<ImageManifest> findImage(const ImageRef& imageRef) const;
    std::vector<ImageManifest> listImages() const;
    fs::path manifestPathFor(const ImageRef& imageRef) const;
    void writeManifest(const ImageManifest& manifest) const;
    void deleteImageAndLayers(const ImageRef& imageRef) const;

    std::optional<std::string> lookupCache(const std::string& cacheKey) const;
    void storeCache(const std::string& cacheKey, const std::string& layerDigest) const;

private:
    fs::path basePath_;

    fs::path cacheIndexPath() const;
};

ImageRef parseImageRef(const std::string& raw);
std::string imageRefToString(const ImageRef& image);

} // namespace docksmith
