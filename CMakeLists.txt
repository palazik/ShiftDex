# ShiftDex - unpack, edit and repack APK and JAR files
# SPDX-License-Identifier: GPL-3.0-or-later
cmake_minimum_required(VERSION 3.19)

project(ShiftDex
    VERSION 1.0.0
    DESCRIPTION "Unpack, edit and repack APK and JAR files"
    HOMEPAGE_URL "https://github.com/palazik/ShiftDex"
    LANGUAGES C CXX)

include(GNUInstallDirs)
include(CTest)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release)
endif()

# Everything the app needs at run time besides its own binary lives in
# <prefix>/lib/shiftdex. The build tree mirrors that layout so the app can
# be started straight from build/bin without installing.
set(SHIFTDEX_LIBDIR "lib/shiftdex")
set(STAGE_BIN "${CMAKE_BINARY_DIR}/bin")
set(STAGE_LIB "${CMAKE_BINARY_DIR}/${SHIFTDEX_LIBDIR}")
file(MAKE_DIRECTORY "${STAGE_LIB}")

# ---------------------------------------------------------------- C: search
add_library(sdxnative STATIC native/sdx_search.c)
target_include_directories(sdxnative PUBLIC native)
target_compile_options(sdxnative PRIVATE -Wall -Wextra)
set_target_properties(sdxnative PROPERTIES POSITION_INDEPENDENT_CODE ON)

# ------------------------------------------------------------ Java: jartool
find_package(Java 11 REQUIRED COMPONENTS Development)
set(JARTOOL_SRC "${CMAKE_SOURCE_DIR}/jartool/src/io/github/palazik/shiftdex/JarTool.java")
set(JARTOOL_CLASSES "${CMAKE_BINARY_DIR}/jartool-classes")
set(JARTOOL_JAR "${STAGE_LIB}/shiftdex-jartool.jar")
add_custom_command(
    OUTPUT "${JARTOOL_JAR}"
    COMMAND "${CMAKE_COMMAND}" -E rm -rf "${JARTOOL_CLASSES}"
    COMMAND "${CMAKE_COMMAND}" -E make_directory "${JARTOOL_CLASSES}"
    COMMAND "${Java_JAVAC_EXECUTABLE}" --release 11 -Xlint:all -encoding UTF-8
            -d "${JARTOOL_CLASSES}" "${JARTOOL_SRC}"
    COMMAND "${Java_JAR_EXECUTABLE}" --create --file "${JARTOOL_JAR}"
            --main-class io.github.palazik.shiftdex.JarTool -C "${JARTOOL_CLASSES}" .
    DEPENDS "${JARTOOL_SRC}"
    COMMENT "Building shiftdex-jartool.jar"
    VERBATIM)
add_custom_target(jartool ALL DEPENDS "${JARTOOL_JAR}")

# ---------------------------------------------------------- Python: engine
# Linked rather than copied, so edits to the engine show up without a rebuild.
add_custom_target(engine ALL
    COMMAND "${CMAKE_COMMAND}" -E create_symlink "${CMAKE_SOURCE_DIR}/engine" "${STAGE_LIB}/engine"
    COMMENT "Linking the engine into the build tree"
    VERBATIM)

# ------------------------------------------------------------------ C++: UI
find_package(Qt6 6.4 REQUIRED COMPONENTS Widgets Svg)

file(GLOB UI_SOURCES CONFIGURE_DEPENDS ui/*.cpp ui/*.h)
add_executable(shiftdex ${UI_SOURCES} ui/resources.qrc)
target_link_libraries(shiftdex PRIVATE Qt6::Widgets Qt6::Svg sdxnative)
target_compile_options(shiftdex PRIVATE -Wall -Wextra)
target_compile_definitions(shiftdex PRIVATE
    SHIFTDEX_VERSION="${PROJECT_VERSION}"
    SHIFTDEX_LIBDIR="${SHIFTDEX_LIBDIR}"
    QT_DISABLE_DEPRECATED_BEFORE=0x060400)
set_target_properties(shiftdex PROPERTIES
    AUTOMOC ON
    AUTORCC ON
    RUNTIME_OUTPUT_DIRECTORY "${STAGE_BIN}")
add_dependencies(shiftdex jartool engine)

# ------------------------------------------------------------------- tests
if(BUILD_TESTING)
    add_executable(sdx_search_test native/sdx_search_test.c)
    target_link_libraries(sdx_search_test PRIVATE sdxnative)
    add_test(NAME native-search
        COMMAND sdx_search_test "${CMAKE_BINARY_DIR}/native-search-test")

    find_package(Python3 COMPONENTS Interpreter)
    if(Python3_FOUND)
        add_test(NAME engine
            COMMAND "${Python3_EXECUTABLE}" -m unittest discover -s "${CMAKE_SOURCE_DIR}/tests" -v)
        set_tests_properties(engine PROPERTIES ENVIRONMENT
            "PYTHONPATH=${CMAKE_SOURCE_DIR}/engine;SHIFTDEX_JARTOOL=${JARTOOL_JAR};PYTHONDONTWRITEBYTECODE=1")
    endif()
endif()

# ----------------------------------------------------------------- install
install(TARGETS shiftdex RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}")
install(FILES "${JARTOOL_JAR}" DESTINATION "${SHIFTDEX_LIBDIR}")
install(DIRECTORY engine/shiftdex DESTINATION "${SHIFTDEX_LIBDIR}/engine"
    FILES_MATCHING PATTERN "*.py" PATTERN "__pycache__" EXCLUDE)
install(FILES data/io.github.palazik.ShiftDex.desktop
    DESTINATION "${CMAKE_INSTALL_DATADIR}/applications")
install(FILES data/io.github.palazik.ShiftDex.metainfo.xml
    DESTINATION "${CMAKE_INSTALL_DATADIR}/metainfo")
install(DIRECTORY data/icons/hicolor DESTINATION "${CMAKE_INSTALL_DATADIR}/icons")
install(FILES docs/PLUGINS.md docs/BUILDING.md README.md LICENSE
    DESTINATION "${CMAKE_INSTALL_DATADIR}/doc/shiftdex")
