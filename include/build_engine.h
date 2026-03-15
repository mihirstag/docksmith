#pragma once

#include <vector>

#include "docksmith_types.h"
#include "state_store.h"

namespace docksmith {

BuildCommandOptions parseBuildOptions(const std::vector<std::string>& args);
BuildResult buildImage(const StateStore& store, const BuildCommandOptions& options);

void runBuildCommand(const StateStore& store, const std::vector<std::string>& args);
void runImagesCommand(const StateStore& store);
void runRmiCommand(const StateStore& store, const std::vector<std::string>& args);

} // namespace docksmith
