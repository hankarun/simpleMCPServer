cmake_minimum_required(VERSION 3.14)
project(CustomMCP VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Use FetchContent for dependencies
include(FetchContent)

# Fetch nlohmann/json
FetchContent_Declare(
    json
    URL https://github.com/nlohmann/json/releases/download/v3.11.3/json.tar.xz
)

# Fetch standalone Asio (header-only)
FetchContent_Declare(
    asio
    GIT_REPOSITORY https://github.com/chriskohlhoff/asio.git
    GIT_TAG asio-1-30-2
)

FetchContent_MakeAvailable(json asio)

# Add executable
add_executable(${PROJECT_NAME} src/main.cpp)

# Link libraries
target_link_libraries(${PROJECT_NAME} 
    PRIVATE 
    nlohmann_json::nlohmann_json
)

# Include Asio headers
target_include_directories(${PROJECT_NAME} PRIVATE ${asio_SOURCE_DIR}/asio/include)

# Define ASIO_STANDALONE for standalone Asio
target_compile_definitions(${PROJECT_NAME} PRIVATE ASIO_STANDALONE)

# Link with pthread on Unix-like systems
if(UNIX)
    target_link_libraries(${PROJECT_NAME} PRIVATE pthread)
endif()

