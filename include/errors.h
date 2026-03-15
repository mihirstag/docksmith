#pragma once

#include <stdexcept>
#include <string>

namespace docksmith {

class DocksmithError : public std::runtime_error {
public:
    explicit DocksmithError(const std::string& message) : std::runtime_error(message) {}
};

} // namespace docksmith
