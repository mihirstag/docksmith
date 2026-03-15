#pragma once

#include <filesystem>
#include <map>
#include <string>
#include <vector>

namespace docksmith {

namespace fs = std::filesystem;

std::string getCurrentUtcIso8601();
std::string sha256Bytes(const std::vector<unsigned char>& bytes);
std::string sha256File(const fs::path& filePath);
std::vector<unsigned char> readBinaryFile(const fs::path& filePath);
void writeTextFile(const fs::path& filePath, const std::string& content);
std::string readTextFile(const fs::path& filePath);
std::string trim(const std::string& input);
bool startsWith(const std::string& value, const std::string& prefix);
std::string joinCommand(const std::vector<std::string>& command);
std::string shellEscapeSingleQuotes(const std::string& input);
std::string normalizeUnixPath(const std::string& path);
std::string toLower(const std::string& value);
std::string serializeSortedEnv(const std::map<std::string, std::string>& env);

} // namespace docksmith
