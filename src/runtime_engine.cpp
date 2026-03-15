#include "runtime_engine.h"

#include <filesystem>
#include <iostream>
#include <map>
#include <string>
#include <vector>

#include "errors.h"
#include "layer_engine.h"
#include "utils.h"

#if defined(__linux__)
#include <cerrno>
#include <csignal>
#include <cstdlib>
#include <cstring>
#include <sys/wait.h>
#include <unistd.h>
#endif

namespace docksmith {
namespace {
std::string stripDigestPrefix(std::string digest) {
    if (digest.rfind("sha256:", 0) == 0) {
        digest = digest.substr(7);
    }
    return digest;
}

std::filesystem::path assembleRootfs(const StateStore& store, const ImageManifest& manifest) {
    const auto tempRoot = std::filesystem::temp_directory_path() /
                          std::filesystem::path("docksmith-rootfs-" + std::to_string(std::rand()));
    std::filesystem::create_directories(tempRoot);

    for (const auto& layer : manifest.layers) {
        const std::string digest = stripDigestPrefix(layer.digest);
        const auto layerPath = store.layersDir() / (digest + ".tar");
        if (!std::filesystem::exists(layerPath)) {
            throw DocksmithError("Layer file missing on disk: " + layerPath.string());
        }
        extractLayerTar(layerPath, tempRoot);
    }

    return tempRoot;
}

std::map<std::string, std::string> envVectorToMap(const std::vector<std::string>& envVec) {
    std::map<std::string, std::string> env;
    for (const auto& kv : envVec) {
        const auto pos = kv.find('=');
        if (pos == std::string::npos || pos == 0) {
            continue;
        }
        env[kv.substr(0, pos)] = kv.substr(pos + 1);
    }
    return env;
}

RunCommandOptions parseRunOptions(const std::vector<std::string>& args) {
    if (args.empty()) {
        throw DocksmithError("docksmith run requires <name:tag>");
    }

    RunCommandOptions options;
    std::size_t i = 0;
    for (; i < args.size(); ++i) {
        if (args[i] == "-e") {
            if (i + 1 >= args.size()) {
                throw DocksmithError("-e expects KEY=VALUE");
            }
            const std::string kv = args[++i];
            const auto pos = kv.find('=');
            if (pos == std::string::npos || pos == 0) {
                throw DocksmithError("Invalid env override: " + kv);
            }
            options.envOverrides[kv.substr(0, pos)] = kv.substr(pos + 1);
            continue;
        }
        break;
    }

    if (i >= args.size()) {
        throw DocksmithError("docksmith run requires <name:tag>");
    }

    options.image = parseImageRef(args[i]);
    ++i;

    for (; i < args.size(); ++i) {
        options.commandOverride.push_back(args[i]);
    }

    return options;
}
} // namespace

int executeIsolated(
    const std::filesystem::path& rootfs,
    const std::vector<std::string>& argv,
    const std::map<std::string, std::string>& env,
    const std::string& workdir) {
    if (argv.empty()) {
        throw DocksmithError("No command specified to execute");
    }

#if !defined(__linux__)
    throw DocksmithError("Container isolation requires Linux. Please run in a Linux VM.");
#else
    const pid_t pid = fork();
    if (pid < 0) {
        throw DocksmithError("fork failed");
    }

    if (pid == 0) {
        if (chdir(rootfs.c_str()) != 0) {
            std::cerr << "chdir(rootfs) failed: " << std::strerror(errno) << "\n";
            _exit(127);
        }

        if (chroot(".") != 0) {
            std::cerr << "chroot failed (need root/CAP_SYS_CHROOT): " << std::strerror(errno) << "\n";
            _exit(127);
        }

        const std::string effectiveWorkdir = workdir.empty() ? "/" : normalizeUnixPath(workdir);
        if (chdir(effectiveWorkdir.c_str()) != 0) {
            std::cerr << "chdir(workdir) failed: " << effectiveWorkdir << "\n";
            _exit(127);
        }

        clearenv();
        for (const auto& [key, value] : env) {
            setenv(key.c_str(), value.c_str(), 1);
        }

        std::vector<char*> cargs;
        cargs.reserve(argv.size() + 1);
        for (const auto& arg : argv) {
            cargs.push_back(const_cast<char*>(arg.c_str()));
        }
        cargs.push_back(nullptr);

        execvp(cargs[0], cargs.data());
        std::cerr << "execvp failed: " << std::strerror(errno) << "\n";
        _exit(127);
    }

    int status = 0;
    if (waitpid(pid, &status, 0) < 0) {
        throw DocksmithError("waitpid failed");
    }

    if (WIFEXITED(status)) {
        return WEXITSTATUS(status);
    }
    if (WIFSIGNALED(status)) {
        return 128 + WTERMSIG(status);
    }
    return 1;
#endif
}

void runContainerCommand(const StateStore& store, const std::vector<std::string>& args) {
    const auto options = parseRunOptions(args);
    const auto manifestOpt = store.findImage(options.image);

    if (!manifestOpt.has_value()) {
        throw DocksmithError("Image not found: " + imageRefToString(options.image));
    }

    const auto& manifest = manifestOpt.value();

    std::vector<std::string> command = options.commandOverride;
    if (command.empty()) {
        command = manifest.config.Cmd;
    }

    if (command.empty()) {
        throw DocksmithError("No command provided and image has no CMD");
    }

    auto env = envVectorToMap(manifest.config.Env);
    for (const auto& [key, value] : options.envOverrides) {
        env[key] = value;
    }

    const auto rootfs = assembleRootfs(store, manifest);

    int exitCode = 1;
    try {
        exitCode = executeIsolated(rootfs, command, env, manifest.config.WorkingDir.empty() ? "/" : manifest.config.WorkingDir);
    } catch (...) {
        std::filesystem::remove_all(rootfs);
        throw;
    }

    std::filesystem::remove_all(rootfs);
    std::cout << "Container exited with code " << exitCode << "\n";

    if (exitCode != 0) {
        throw DocksmithError("Container process failed with exit code " + std::to_string(exitCode));
    }
}

} // namespace docksmith
