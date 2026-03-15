#pragma once

#include <filesystem>
#include <string>
#include <vector>

namespace docksmith {

namespace fs = std::filesystem;

enum class InstructionType {
    From,
    Copy,
    Run,
    Workdir,
    Env,
    Cmd,
};

struct Instruction {
    InstructionType type;
    std::string rawText;
    std::string argText;
    int lineNumber{};
};

struct ParsedDocksmithfile {
    std::vector<Instruction> instructions;
};

ParsedDocksmithfile parseDocksmithfile(const fs::path& filePath);

} // namespace docksmith
