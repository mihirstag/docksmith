#include <iostream>
#include <string>
#include <filesystem>
#include <vector>
#include <fstream>
#include <iomanip>
#include <nlohmann/json.hpp>
#include "manifest.h"

namespace fs = std::filesystem;
using json = nlohmann::json;

// Global base path variable
std::string getBasePath() {
    const char* homeDir = getenv("HOME");
    if (homeDir == nullptr) {
        std::cerr << "Error: HOME environment variable not set." << std::endl;
        exit(1);
    }
    return std::string(homeDir) + "/.docksmith";
}

void initializeState() {
    std::string basePath = getBasePath();
    std::vector<std::string> dirs = {
        basePath, basePath + "/images", basePath + "/layers", basePath + "/cache"
    };
    for (const auto& dir : dirs) {
        if (!fs::exists(dir)) {
            fs::create_directories(dir);
        }
    }
}

void listImages() {
    std::string imagesDir = getBasePath() + "/images";
    std::cout << std::left << std::setw(20) << "NAME" << std::setw(15) << "TAG" << std::setw(15) << "ID" << "CREATED\n";

    for (const auto& entry : fs::directory_iterator(imagesDir)) {
        if (entry.path().extension() == ".json") {
            std::ifstream file(entry.path());
            if (file.is_open()) {
                try {
                    json j;
                    file >> j;
                    std::string name = j.value("name", "<none>");
                    std::string tag = j.value("tag", "<none>");
                    std::string digest = j.value("digest", "");
                    std::string created = j.value("created", "<unknown>");

                    std::string id = (digest.find("sha256:") == 0 && digest.length() >= 19) ? digest.substr(7, 12) : "";

                    std::cout << std::left << std::setw(20) << name << std::setw(15) << tag << std::setw(15) << id << created << "\n";
                } catch (...) {
                    // Ignore malformed JSONs silently in the list view
                }
            }
        }
    }
}

// Function to remove an image and its layers
void removeImage(const std::string& targetImage) {
    std::string imagesDir = getBasePath() + "/images";
    std::string layersDir = getBasePath() + "/layers";
    bool found = false;

    for (const auto& entry : fs::directory_iterator(imagesDir)) {
        if (entry.path().extension() == ".json") {
            std::ifstream file(entry.path());
            if (file.is_open()) {
                try {
                    json j;
                    file >> j;
                    std::string name = j.value("name", "");
                    std::string tag = j.value("tag", "");
                    std::string fullName = name + ":" + tag;

                    if (fullName == targetImage) {
                        found = true;
                        file.close(); // Close the file stream before trying to delete the file

                        // 1. Delete associated layers
                        if (j.contains("layers") && j["layers"].is_array()) {
                            for (const auto& layer : j["layers"]) {
                                std::string digest = layer.value("digest", "");
                                if (!digest.empty()) {
                                    // Remove 'sha256:' prefix if present to match filename
                                    std::string filename = (digest.find("sha256:") == 0) ? digest.substr(7) : digest;
                                    fs::path layerPath = fs::path(layersDir) / (filename + ".tar"); // Assuming teammates save as .tar
                                    
                                    if (fs::exists(layerPath)) {
                                        fs::remove(layerPath);
                                        std::cout << "Deleted layer file: " << layerPath.filename() << "\n";
                                    }
                                }
                            }
                        }

                        // 2. Delete the manifest
                        fs::remove(entry.path());
                        std::cout << "Untagged and removed image: " << targetImage << "\n";
                        break;
                    }
                } catch (...) {
                    file.close();
                }
            }
        }
    }

    if (!found) {
        std::cerr << "Error: No such image: " << targetImage << "\n";
        exit(1);
    }
}

int main(int argc, char* argv[]) {
    initializeState();

    if (argc < 2) {
        std::cerr << "Usage: docksmith <command> [args]\n";
        return 1;
    }

    std::string command = argv[1];
    std::vector<std::string> args(argv + 2, argv + argc);

    if (command == "build") {
        // [Hand off to Build Engine Teammate]
        std::cout << "[CLI] Routing to Build Engine...\n";
    } 
    else if (command == "run") {
        // [Hand off to Runtime Engine Teammate]
        std::cout << "[CLI] Routing to Runtime Engine...\n";
    } 
    else if (command == "images") {
        listImages();
    } 
    else if (command == "rmi") {
        if (args.empty()) {
            std::cerr << "Error: docksmith rmi requires an image <name:tag>\n";
            return 1;
        }
        removeImage(args[0]);
    } 
    else {
        std::cerr << "Unknown command: " << command << "\n";
        return 1;
    }

    return 0;
}