# ONGA Sync: the desktop app that installs, updates and uninstalls ONGA plug-ins.
#
#   cmake -S app -B build -DCMAKE_BUILD_TYPE=Release
#   cmake --build build --target OngaSync OngaSyncCli
#
# Development builds offer SKIP SIGN-IN (a developer account licensed for everything).
# Turn it off for a release once ONGA accounts are live: -DONGA_SYNC_DEV_BYPASS=OFF
cmake_minimum_required(VERSION 3.22)

set(CMAKE_OSX_DEPLOYMENT_TARGET "11.0" CACHE STRING "Minimum macOS")
project(OngaSync VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

option(ONGA_SYNC_DEV_BYPASS "Offer SKIP SIGN-IN with a developer account" ON)
option(ONGA_SYNC_BUILD_APP "Build the app (needs a GUI toolkit)" ON)
set(ONGA_SYNC_CATALOG_URL "https://github.com/benettriley/onga_sync/releases/latest/download/catalog.json"
    CACHE STRING "Where the app reads catalog.json by default")

# ---------------------------------------------------------------- JUCE
# Point JUCE_DIR at a local checkout to skip the download:  -DJUCE_DIR=/path/to/JUCE
set(JUCE_DIR "" CACHE PATH "Local JUCE 8 checkout (optional)")
if (JUCE_DIR)
    add_subdirectory(${JUCE_DIR} ${CMAKE_BINARY_DIR}/JUCE)
else()
    include(FetchContent)
    FetchContent_Declare(JUCE
        GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
        GIT_TAG        8.0.15
        GIT_SHALLOW    TRUE)
    FetchContent_MakeAvailable(JUCE)
endif()

set(ONGA_UI_DIR "${CMAKE_CURRENT_LIST_DIR}/../onga-ui" CACHE PATH "The onga-ui kit (git submodule)")

set(CORE_SOURCES
    Source/core/Account.cpp
    Source/core/Catalog.cpp
    Source/core/Download.cpp
    Source/core/Installed.cpp
    Source/core/Scripts.cpp)

set(COMMON_DEFINITIONS
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_STRICT_REFCOUNTEDPOINTER=1
    ONGA_SYNC_VERSION="${PROJECT_VERSION}"
    ONGA_SYNC_DEV_BYPASS=$<BOOL:${ONGA_SYNC_DEV_BYPASS}>
    ONGA_SYNC_CATALOG_URL="${ONGA_SYNC_CATALOG_URL}")

# ---------------------------------------------------------------- the app
if (ONGA_SYNC_BUILD_APP)
    add_subdirectory(${ONGA_UI_DIR} ${CMAKE_BINARY_DIR}/onga-ui)

    juce_add_gui_app(OngaSync
        PRODUCT_NAME    "ONGA Sync"
        COMPANY_NAME    "ONGA"
        BUNDLE_ID       com.ongatools.sync
        VERSION         ${PROJECT_VERSION}
        DOCUMENT_WINDOW_AUTO_SIZE FALSE)

    target_sources(OngaSync PRIVATE
        ${CORE_SOURCES}
        Source/app/Engine.cpp
        Source/app/Main.cpp
        Source/ui/LibraryScreen.cpp
        Source/ui/MainPanel.cpp
        Source/ui/SignInView.cpp)

    target_compile_definitions(OngaSync PRIVATE ${COMMON_DEFINITIONS}
        JUCE_APPLICATION_NAME_STRING="$<TARGET_PROPERTY:OngaSync,JUCE_PRODUCT_NAME>"
        JUCE_APPLICATION_VERSION_STRING="$<TARGET_PROPERTY:OngaSync,JUCE_VERSION>")

    target_link_libraries(OngaSync
        PRIVATE
            onga_ui
            juce::juce_cryptography
            juce::juce_gui_basics
        PUBLIC
            juce::juce_recommended_config_flags
            juce::juce_recommended_lto_flags
            juce::juce_recommended_warning_flags)
endif()

# ---------------------------------------------------------------- headless core + tests
juce_add_console_app(OngaSyncCli)
target_sources(OngaSyncCli PRIVATE ${CORE_SOURCES} Source/cli/Cli.cpp)
target_compile_definitions(OngaSyncCli PRIVATE ${COMMON_DEFINITIONS})
target_link_libraries(OngaSyncCli
    PRIVATE
        juce::juce_core
        juce::juce_cryptography
        juce::juce_events
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_warning_flags)

enable_testing()
add_test(NAME core COMMAND OngaSyncCli test)
