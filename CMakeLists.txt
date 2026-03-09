cmake_minimum_required(VERSION 3.22)
project(SSA VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(FetchContent)

# -------- GLFW (windowing / input) --------
set(GLFW_BUILD_DOCS OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)

FetchContent_Declare(
    glfw
    GIT_REPOSITORY https://github.com/glfw/glfw.git
    GIT_TAG        3.4
)

# -------- Dear ImGui (source-only, no native CMake) --------
FetchContent_Declare(
    imgui
    GIT_REPOSITORY https://github.com/ocornut/imgui.git
    GIT_TAG        v1.91.8
)

# -------- nlohmann/json --------
FetchContent_Declare(
    nlohmann_json
    GIT_REPOSITORY https://github.com/nlohmann/json.git
    GIT_TAG        v3.11.3
)

# -------- Google Test --------
FetchContent_Declare(
    googletest
    GIT_REPOSITORY https://github.com/google/googletest.git
    GIT_TAG        v1.15.2
)

FetchContent_MakeAvailable(glfw imgui nlohmann_json googletest)

# -------- OpenGL --------
find_package(OpenGL REQUIRED)

# -------- Threads (needed on Linux for pthreads) --------
if(UNIX AND NOT APPLE)
    set(THREADS_PREFER_PTHREAD_FLAG ON)
    find_package(Threads REQUIRED)
endif()

# -------- Build imgui as a static library --------
add_library(imgui STATIC
    ${imgui_SOURCE_DIR}/imgui.cpp
    ${imgui_SOURCE_DIR}/imgui_demo.cpp
    ${imgui_SOURCE_DIR}/imgui_draw.cpp
    ${imgui_SOURCE_DIR}/imgui_tables.cpp
    ${imgui_SOURCE_DIR}/imgui_widgets.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_glfw.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_opengl3.cpp
)

target_include_directories(imgui PUBLIC
    ${imgui_SOURCE_DIR}
    ${imgui_SOURCE_DIR}/backends
)

target_link_libraries(imgui PUBLIC
    glfw
    OpenGL::GL
)

# ---------- Main application ----------
set(APP_SOURCES
    src/main.cpp
    src/MainWindow.cpp
    src/MainWindow.hpp
    src/HomeDashboard.cpp
    src/HomeDashboard.hpp
    src/ServerTabWidget.cpp
    src/ServerTabWidget.hpp
    src/ServerManager.cpp
    src/ServerManager.hpp
    src/ServerConfig.hpp
    src/BackupModule.cpp
    src/BackupModule.hpp
    src/SteamCmdModule.cpp
    src/SteamCmdModule.hpp
    src/RconClient.cpp
    src/RconClient.hpp
    src/SchedulerModule.cpp
    src/SchedulerModule.hpp
    src/LogModule.cpp
    src/LogModule.hpp
    src/TrayManager.cpp
    src/TrayManager.hpp
    src/WebhookModule.cpp
    src/WebhookModule.hpp
    src/ConsoleLogWriter.hpp
    src/ResourceMonitor.cpp
    src/ResourceMonitor.hpp
    src/EventHookManager.cpp
    src/EventHookManager.hpp
)

add_executable(SSA ${APP_SOURCES})

target_include_directories(SSA PRIVATE src)

target_link_libraries(SSA PRIVATE
    imgui
    glfw
    OpenGL::GL
    nlohmann_json::nlohmann_json
)

if(UNIX AND NOT APPLE)
    target_link_libraries(SSA PRIVATE Threads::Threads)
endif()

# ---------- Tests ----------
enable_testing()
include(GoogleTest)

set(TEST_SOURCES
    tests/test_serverconfig.cpp
    src/ServerConfig.hpp
    src/ServerManager.cpp
    src/ServerManager.hpp
    src/BackupModule.cpp
    src/BackupModule.hpp
    src/SteamCmdModule.cpp
    src/SteamCmdModule.hpp
    src/RconClient.cpp
    src/RconClient.hpp
    src/SchedulerModule.cpp
    src/SchedulerModule.hpp
    src/LogModule.cpp
    src/LogModule.hpp
    src/WebhookModule.cpp
    src/WebhookModule.hpp
    src/ConsoleLogWriter.hpp
    src/ResourceMonitor.cpp
    src/ResourceMonitor.hpp
    src/EventHookManager.cpp
    src/EventHookManager.hpp
)

add_executable(SSA_Tests ${TEST_SOURCES})

target_include_directories(SSA_Tests PRIVATE src)

target_link_libraries(SSA_Tests PRIVATE
    nlohmann_json::nlohmann_json
    GTest::gtest_main
)

if(UNIX AND NOT APPLE)
    target_link_libraries(SSA_Tests PRIVATE Threads::Threads)
endif()

gtest_discover_tests(SSA_Tests)
