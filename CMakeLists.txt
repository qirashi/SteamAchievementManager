cmake_minimum_required(VERSION 3.21)

project(SteamAchievementManager
    VERSION 8.0.0
    DESCRIPTION "Steam Achievement Manager (Qt 6)"
    LANGUAGES CXX)

if(NOT WIN32)
    message(FATAL_ERROR "Steam Achievement Manager only supports Windows.")
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# `cmake --install` produces a portable folder (./release by default):
# executables and Qt DLLs side by side.
if(CMAKE_INSTALL_PREFIX_INITIALIZED_TO_DEFAULT)
    set(CMAKE_INSTALL_PREFIX "${CMAKE_CURRENT_SOURCE_DIR}/release" CACHE PATH "" FORCE)
endif()
set(CMAKE_INSTALL_BINDIR ".")

find_package(Qt6 6.10 REQUIRED COMPONENTS Core Gui Widgets Network Svg LinguistTools)
qt_standard_project_setup(REQUIRES 6.10)

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")

configure_file(src/core/Version.h.in "${CMAKE_BINARY_DIR}/generated/Version.h" @ONLY)

# ---------------------------------------------------------------------------
# Shared code: Steam client bindings, schema parser, image cache, styling.
# ---------------------------------------------------------------------------
qt_add_library(sam_core STATIC
    src/core/Application.cpp
    src/core/Application.h
    src/core/Icons.cpp
    src/core/Icons.h
    src/core/ImageCache.cpp
    src/core/ImageCache.h
    src/core/KeyValue.cpp
    src/core/KeyValue.h
    src/core/Languages.cpp
    src/core/Languages.h
    src/core/SteamClient.cpp
    src/core/SteamClient.h
)
target_include_directories(sam_core PUBLIC src/core "${CMAKE_BINARY_DIR}/generated")
target_link_libraries(sam_core PUBLIC Qt6::Widgets Qt6::Network Qt6::Svg)
qt_add_resources(sam_core "sam_assets"
    PREFIX "/sam"
    BASE resources
    FILES resources/SAM.ico)
target_compile_definitions(sam_core PUBLIC
    UNICODE _UNICODE NOMINMAX WIN32_LEAN_AND_MEAN
    QT_NO_CAST_FROM_ASCII QT_NO_URL_CAST_FROM_STRING)
if(MSVC)
    target_compile_options(sam_core PUBLIC /W4 /utf-8 /permissive-)
endif()

# ---------------------------------------------------------------------------
# SAM.Picker: the game list.
# ---------------------------------------------------------------------------
qt_add_executable(SAM.Picker WIN32
    src/picker/GameDelegate.cpp
    src/picker/GameDelegate.h
    src/picker/GameListModel.cpp
    src/picker/GameListModel.h
    src/picker/PickerWindow.cpp
    src/picker/PickerWindow.h
    src/picker/main.cpp
    resources/SAM.rc
)
target_link_libraries(SAM.Picker PRIVATE sam_core)

# ---------------------------------------------------------------------------
# SAM.Game: the achievement/statistics manager for a single game.
# ---------------------------------------------------------------------------
qt_add_executable(SAM.Game WIN32
    src/game/AchievementDelegate.cpp
    src/game/AchievementDelegate.h
    src/game/AchievementGroups.cpp
    src/game/AchievementGroups.h
    src/game/AchievementModel.cpp
    src/game/AchievementModel.h
    src/game/ManagerWindow.cpp
    src/game/ManagerWindow.h
    src/game/Schema.cpp
    src/game/Schema.h
    src/game/StatsModel.cpp
    src/game/StatsModel.h
    src/game/main.cpp
    resources/SAM.rc
)
target_link_libraries(SAM.Game PRIVATE sam_core)

# ---------------------------------------------------------------------------
# Translations (embedded into both executables under :/i18n).
# Run the "update_translations" target to refresh the .ts files.
# ---------------------------------------------------------------------------
set(SAM_TS_FILES translations/sam_ru.ts)
qt_add_lupdate(
    TS_FILES ${SAM_TS_FILES}
    SOURCE_TARGETS sam_core SAM.Picker SAM.Game
    OPTIONS -no-obsolete -locations none)
qt_add_lrelease(
    TS_FILES ${SAM_TS_FILES}
    QM_FILES_OUTPUT_VARIABLE SAM_QM_FILES)
foreach(target IN ITEMS SAM.Picker SAM.Game)
    qt_add_resources(${target} "${target}_i18n"
        PREFIX "/i18n"
        BASE "${CMAKE_CURRENT_BINARY_DIR}"
        FILES ${SAM_QM_FILES})
endforeach()

# ---------------------------------------------------------------------------
# Install + deploy Qt runtime next to the executables:
#   cmake --install <build-dir>
# ---------------------------------------------------------------------------
install(TARGETS SAM.Picker SAM.Game RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}")
foreach(target IN ITEMS SAM.Picker SAM.Game)
    qt_generate_deploy_app_script(
        TARGET ${target}
        OUTPUT_SCRIPT deploy_script
        DEPLOY_TOOL_OPTIONS --no-opengl-sw --no-compiler-runtime --no-system-dxc-compiler
                            --no-system-d3d-compiler --translations en,ru)
    install(SCRIPT ${deploy_script})
endforeach()
install(FILES "${CMAKE_CURRENT_SOURCE_DIR}/LICENSE.txt" DESTINATION .)
