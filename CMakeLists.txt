cmake_minimum_required(VERSION 3.22)
project(SSA VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

# Qt6 components required
find_package(Qt6 COMPONENTS Core Widgets Network)
if(NOT Qt6_FOUND)
    message(FATAL_ERROR
        "Qt6 was not found.\n"
        "  Linux  : sudo apt-get install qt6-base-dev   (or dnf/pacman equivalent)\n"
        "  macOS  : brew install qt@6\n"
        "  Windows: install Qt 6.4+ from https://www.qt.io/download\n"
        "\n"
        "Then either:\n"
        "  • Re-run cmake so it can discover Qt automatically, or\n"
        "  • Pass the install location explicitly:\n"
        "      cmake -B build -DCMAKE_PREFIX_PATH=\"/path/to/Qt/6.x.x/gcc_64\"\n"
        "\n"
        "Alternatively, use the provided build scripts which handle this automatically:\n"
        "  Linux/macOS : scripts/build.sh\n"
        "  Windows     : scripts/build.ps1\n"
    )
endif()

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
)

qt_add_executable(SSA ${APP_SOURCES})

target_link_libraries(SSA PRIVATE
    Qt6::Core
    Qt6::Widgets
    Qt6::Network
)

target_include_directories(SSA PRIVATE src)

# ---------- Tests ----------
enable_testing()
find_package(Qt6 OPTIONAL_COMPONENTS Test)

if(Qt6Test_FOUND)
    qt_add_executable(SSA_Tests
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
    )

    target_link_libraries(SSA_Tests PRIVATE
        Qt6::Core
        Qt6::Network
        Qt6::Test
    )

    target_include_directories(SSA_Tests PRIVATE src)

    add_test(NAME SSA_Tests COMMAND SSA_Tests)
endif()
