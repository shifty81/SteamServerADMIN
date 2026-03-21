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

# -------- Portable File Dialogs (header-only) --------
# Note: v0.1.0 has cmake_minimum_required(VERSION 3.1) which is incompatible
# with CMake >= 3.30.  We download it and create the INTERFACE target manually,
# skipping its broken CMakeLists.txt.
FetchContent_Declare(
    portable_file_dialogs
    GIT_REPOSITORY https://github.com/samhocevar/portable-file-dialogs.git
    GIT_TAG        0.1.0
)

FetchContent_MakeAvailable(glfw imgui nlohmann_json googletest)

# Suppress CMP0169 deprecation warning for FetchContent_Populate
if(POLICY CMP0169)
    cmake_policy(PUSH)
    cmake_policy(SET CMP0169 OLD)
endif()
FetchContent_GetProperties(portable_file_dialogs)
if(NOT portable_file_dialogs_POPULATED)
    FetchContent_Populate(portable_file_dialogs)
    add_library(portable_file_dialogs INTERFACE)
    target_include_directories(portable_file_dialogs INTERFACE "${portable_file_dialogs_SOURCE_DIR}")
endif()
if(POLICY CMP0169)
    cmake_policy(POP)
endif()

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
    src/SteamLibraryDetector.cpp
    src/SteamLibraryDetector.hpp
    src/IniEditor.cpp
    src/IniEditor.hpp
    src/ConfigBackupManager.cpp
    src/ConfigBackupManager.hpp
    src/GracefulRestartManager.cpp
    src/GracefulRestartManager.hpp
    src/ConfigFileDiscovery.hpp
    src/SteamQueryClient.cpp
    src/SteamQueryClient.hpp
)

add_executable(SSA ${APP_SOURCES})

target_include_directories(SSA PRIVATE src)

if(WIN32)
    target_compile_definitions(SSA PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
    target_link_libraries(SSA PRIVATE ws2_32)
endif()

target_link_libraries(SSA PRIVATE
    imgui
    glfw
    OpenGL::GL
    nlohmann_json::nlohmann_json
    portable_file_dialogs
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
    src/SteamLibraryDetector.cpp
    src/SteamLibraryDetector.hpp
    src/IniEditor.cpp
    src/IniEditor.hpp
    src/ConfigBackupManager.cpp
    src/ConfigBackupManager.hpp
    src/GracefulRestartManager.cpp
    src/GracefulRestartManager.hpp
    src/ConfigFileDiscovery.hpp
    src/SteamQueryClient.cpp
    src/SteamQueryClient.hpp
)

add_executable(SSA_Tests ${TEST_SOURCES})

target_include_directories(SSA_Tests PRIVATE src)

if(WIN32)
    target_compile_definitions(SSA_Tests PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
    target_link_libraries(SSA_Tests PRIVATE ws2_32)
endif()

target_link_libraries(SSA_Tests PRIVATE
    nlohmann_json::nlohmann_json
    GTest::gtest_main
)

if(UNIX AND NOT APPLE)
    target_link_libraries(SSA_Tests PRIVATE Threads::Threads)
endif()

gtest_discover_tests(SSA_Tests)
