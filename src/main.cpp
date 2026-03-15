#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

#include "build_engine.h"
#include "errors.h"
#include "runtime_engine.h"
#include "state_store.h"

namespace {
void printUsage() {
    std::cerr
        << "Usage:\n"
        << "  docksmith build -t <name:tag> [--no-cache] <context>\n"
        << "  docksmith images\n"
        << "  docksmith rmi <name:tag>\n"
        << "  docksmith run [-e KEY=VALUE ...] <name:tag> [cmd ...]\n";
}
} // namespace

int main(int argc, char* argv[]) {
    try {
        docksmith::StateStore store;
        store.initializeState();

        if (argc < 2) {
            printUsage();
            return 1;
        }

        const std::string command = argv[1];
        std::vector<std::string> args(argv + 2, argv + argc);

        if (command == "build") {
            docksmith::runBuildCommand(store, args);
            return 0;
        }

        if (command == "images") {
            docksmith::runImagesCommand(store);
            return 0;
        }

        if (command == "rmi") {
            docksmith::runRmiCommand(store, args);
            return 0;
        }

        if (command == "run") {
            docksmith::runContainerCommand(store, args);
            return 0;
        }

        std::cerr << "Unknown command: " << command << "\n";
        printUsage();
        return 1;
    } catch (const docksmith::DocksmithError& ex) {
        std::cerr << "Error: " << ex.what() << "\n";
        return 1;
    } catch (const std::exception& ex) {
        std::cerr << "Fatal: " << ex.what() << "\n";
        return 1;
    }
}