cmake_minimum_required(VERSION 3.24)

project(ExAccountManager
    VERSION 0.1.0
    DESCRIPTION "ExAccountManager - Roblox multi-account manager"
    LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if (MSVC)
    # UTF-8 source/exec-charset (RU/EN UI strings), strong warnings, parallel /GL on release
    add_compile_options(/utf-8 /W4 /permissive- /Zc:preprocessor)
    add_compile_definitions(_CRT_SECURE_NO_WARNINGS NOMINMAX WIN32_LEAN_AND_MEAN UNICODE _UNICODE)
endif()

add_compile_definitions(EX_VERSION_STRING="0.1.0" SODIUM_STATIC=1)

# ------------------------------------------------------------------ Qt
list(APPEND CMAKE_PREFIX_PATH "$ENV{QT_DIR}")
find_package(Qt6 6.5 REQUIRED COMPONENTS Core Gui Widgets Svg Network Test)
message(STATUS "Qt6 version: ${Qt6Core_VERSION}")

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

# ------------------------------------------------------------------ third party: sqlite3 (amalgamation, public domain)
add_library(ex_sqlite3 STATIC third_party/sqlite3/sqlite3.c)
target_include_directories(ex_sqlite3 PUBLIC third_party/sqlite3)
target_compile_definitions(ex_sqlite3 PRIVATE SQLITE_ENABLE_FTS5 SQLITE_ENABLE_JSON1 SQLITE_OMIT_LOAD_EXTENSION SQLITE_THREADSAFE=1)
if (MSVC)
    target_compile_options(ex_sqlite3 PRIVATE /wd4996)
endif()

# ------------------------------------------------------------------ third party: libsodium (prebuilt static, ISC)
set(SODIUM_DIR ${CMAKE_CURRENT_SOURCE_DIR}/third_party/libsodium)
add_library(ex_sodium INTERFACE)
target_include_directories(ex_sodium INTERFACE ${SODIUM_DIR}/include)
target_link_libraries(ex_sodium INTERFACE ${SODIUM_DIR}/x64/Release/v143/static/libsodium.lib)

# ------------------------------------------------------------------ ex_platform: Win32 layer (no Qt)
file(GLOB_RECURSE EX_PLATFORM_SOURCES CONFIGURE_DEPENDS src/platform/*.cpp)
add_library(ex_platform STATIC ${EX_PLATFORM_SOURCES})
target_include_directories(ex_platform PUBLIC src)
target_link_libraries(ex_platform PUBLIC ex_sodium Qt6::Core advapi32 psapi user32 shell32 dwmapi)

# ------------------------------------------------------------------ ex_core: domain logic (QtCore + QtNetwork only)
file(GLOB_RECURSE EX_CORE_SOURCES CONFIGURE_DEPENDS src/core/*.cpp)
add_library(ex_core STATIC ${EX_CORE_SOURCES})
target_include_directories(ex_core PUBLIC src)
target_link_libraries(ex_core
    PUBLIC Qt6::Core Qt6::Network
    PUBLIC ex_platform ex_sqlite3 ex_sodium)

# ------------------------------------------------------------------ ex_ui: Qt Widgets layer
file(GLOB_RECURSE EX_UI_SOURCES CONFIGURE_DEPENDS src/ui/*.cpp)
add_library(ex_ui STATIC ${EX_UI_SOURCES})
target_include_directories(ex_ui PUBLIC src)
target_link_libraries(ex_ui PUBLIC Qt6::Widgets Qt6::Svg ex_core)

# ------------------------------------------------------------------ application
qt_add_executable(ExAccountManager WIN32
    src/main.cpp
    src/ui/resources.qrc)
target_link_libraries(ExAccountManager PRIVATE ex_ui)

set_target_properties(ExAccountManager PROPERTIES
    OUTPUT_NAME "ExAccountManager"
    WIN32_EXECUTABLE TRUE)

if (MSVC)
    target_compile_options(ExAccountManager PRIVATE $<$<CONFIG:Release>:/GL>)
    target_link_options(ExAccountManager PRIVATE $<$<CONFIG:Release>:/LTCG>)
endif()

# ------------------------------------------------------------------ tests
enable_testing()
find_package(Qt6 REQUIRED COMPONENTS Test)
file(GLOB EX_TEST_SOURCES CONFIGURE_DEPENDS tests/*.cpp)
if (EX_TEST_SOURCES)
    qt_add_executable(ex_tests ${EX_TEST_SOURCES})
    target_link_libraries(ex_tests PRIVATE Qt6::Test ex_core ex_platform)
    add_test(NAME ex_tests COMMAND ex_tests)
    # Tests must be able to find Qt DLLs next to the app or in Qt bin
    set_tests_properties(ex_tests PROPERTIES ENVIRONMENT_MODIFICATION "PATH=path_list_prepend:$<TARGET_FILE_DIR:Qt6::Core>")
endif()

# ------------------------------------------------------------------ tools
add_executable(vault_check tools/vault_check.cpp)
target_link_libraries(vault_check PRIVATE ex_core ex_platform)

# ------------------------------------------------------------------ install (portable layout)
install(TARGETS ExAccountManager RUNTIME DESTINATION .)
