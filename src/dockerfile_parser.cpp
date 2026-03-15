#include "dockerfile_parser.h"

#include <fstream>
#include <sstream>

#include "errors.h"
#include "utils.h"

namespace docksmith {
namespace {
InstructionType parseInstructionType(const std::string& keyword, int lineNumber) {
    if (keyword == "FROM") return InstructionType::From;
    if (keyword == "COPY") return InstructionType::Copy;
    if (keyword == "RUN") return InstructionType::Run;
    if (keyword == "WORKDIR") return InstructionType::Workdir;
    if (keyword == "ENV") return InstructionType::Env;
    if (keyword == "CMD") return InstructionType::Cmd;
    throw DocksmithError("Unknown instruction at line " + std::to_string(lineNumber) + ": " + keyword);
}
} // namespace

ParsedDocksmithfile parseDocksmithfile(const fs::path& filePath) {
    std::ifstream file(filePath);
    if (!file) {
        throw DocksmithError("Docksmithfile not found at: " + filePath.string());
    }

    ParsedDocksmithfile parsed;
    std::string rawLine;
    int lineNo = 0;

    while (std::getline(file, rawLine)) {
        ++lineNo;
        const std::string line = trim(rawLine);
        if (line.empty() || startsWith(line, "#")) {
            continue;
        }

        std::istringstream iss(line);
        std::string keyword;
        iss >> keyword;

        if (keyword.empty()) {
            continue;
        }

        const InstructionType type = parseInstructionType(keyword, lineNo);
        std::string rest;
        std::getline(iss, rest);
        rest = trim(rest);

        if (rest.empty()) {
            throw DocksmithError("Missing argument at line " + std::to_string(lineNo) + ": " + line);
        }

        Instruction instruction;
        instruction.type = type;
        instruction.rawText = line;
        instruction.argText = rest;
        instruction.lineNumber = lineNo;

        parsed.instructions.push_back(std::move(instruction));
    }

    if (parsed.instructions.empty()) {
        throw DocksmithError("Docksmithfile has no instructions");
    }

    return parsed;
}

} // namespace docksmith
