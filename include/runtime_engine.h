#pragma once

#include <map>
#include <string>
#include <vector>

#include "docksmith_types.h"
#include "state_store.h"

namespace docksmith {

int executeIsolated(
    const std::filesystem::path& rootfs,
    const std::vector<std::string>& argv,
    const std::map<std::string, std::string>& env,
    const std::string& workdir);

void runContainerCommand(const StateStore& store, const std::vector<std::string>& args);

} // namespace docksmith
