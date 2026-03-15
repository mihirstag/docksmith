cmake_minimum_required(VERSION 3.10)
project(docksmith)

# Set C++ standard
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED True)

# Add the include directory so we can find our header files
include_directories(include)

# Find the nlohmann-json library we installed earlier
find_package(nlohmann_json REQUIRED)

# Define our executable
add_executable(docksmith src/main.cpp)

# Link the JSON library to our executable
target_link_libraries(docksmith PRIVATE nlohmann_json::nlohmann_json)