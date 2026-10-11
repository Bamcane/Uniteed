cmake_minimum_required(VERSION 3.16)

file(STRINGS src/base/define.h GAME_NAME_LINE REGEX "^#define[ \t]+GAME_NAME[ \t]+")
if(GAME_NAME_LINE)
  string(REGEX MATCH "\"([^\"]*)\"" _ ${GAME_NAME_LINE})
  set(GAME_NAME ${CMAKE_MATCH_1})
endif()
if(NOT GAME_NAME)
  set(GAME_NAME uniteed)
endif()

project(${GAME_NAME} C CXX)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
# libsugarx, TWICE and SandboxCmd all target C++23
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

option(UNITEED_GUI "build the graphical editor (needs sdl3, freetype, harfbuzz)" ON)
option(UNITEED_WERROR "treat warnings as errors" OFF)

##################### system libraries, kept as light as possible
find_package(PkgConfig REQUIRED)
pkg_check_modules(SDL3 sdl3)
pkg_check_modules(FREETYPE freetype2)
pkg_check_modules(HARFBUZZ harfbuzz)
pkg_check_modules(ZLIB REQUIRED zlib)
pkg_check_modules(ZSTD libzstd)
# opusfile is only needed for the sound preview in the windowed editor, so it
# is optional and the headless build never sees it
pkg_check_modules(OPUSFILE opusfile)
find_package(OpenSSL)

# the window is SDL3, the text is FreeType with HarfBuzz shaping; without them
# the editor cannot be built and only the command line remains
if(UNITEED_GUI AND NOT (SDL3_FOUND AND FREETYPE_FOUND AND HARFBUZZ_FOUND))
  if(NOT SDL3_FOUND)
    message(STATUS "sdl3 not found, falling back to the headless build")
  elseif(NOT FREETYPE_FOUND)
    message(STATUS "freetype2 not found, falling back to the headless build")
  else()
    message(STATUS "harfbuzz not found, falling back to the headless build")
  endif()
  set(UNITEED_GUI OFF)
endif()

##################### vendored / submodule libraries
# libsugarx is a git submodule, header only
if(NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/external/libsugarx/sugar_types.h")
  message(FATAL_ERROR "the libsugarx submodule is missing, run: git submodule update --init --recursive")
endif()

# stb is a git submodule too; the build only uses its PNG decoder, and the
# headless build needs it as well, so it is required either way
if(NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/external/stb/stb_image.h")
  message(FATAL_ERROR "the stb submodule is missing, run: git submodule update --init --recursive")
endif()

##################### sources
file(GLOB_RECURSE BASE_SOURCES CONFIGURE_DEPENDS "src/base/*.cpp"   "src/base/*.h")
file(GLOB_RECURSE CONFIG_SOURCES CONFIGURE_DEPENDS "src/config/*.cpp" "src/config/*.h")
file(GLOB_RECURSE GAME_SOURCES CONFIGURE_DEPENDS "src/game/*.cpp"   "src/game/*.h")
set(SOURCES ${BASE_SOURCES} ${CONFIG_SOURCES} ${GAME_SOURCES})
# the entry point is always part of the build, the window code is optional
list(APPEND SOURCES "src/engine/main.cpp")

if(UNITEED_GUI)
  # the editor window pulls in the engine layer and the painter, the headless
  # build only needs the command line
  file(GLOB ENGINE_SOURCES CONFIGURE_DEPENDS "src/engine/*.cpp" "src/engine/*.h")
  list(REMOVE_ITEM ENGINE_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/src/engine/main.cpp")
  list(APPEND SOURCES ${ENGINE_SOURCES} "src/game/editor/editor.cpp" "src/game/editor/editor_draw.cpp")
else()
  # The editor *model* is part of the command line too: the agent interface and
  # the project commands drive the same CEditor the window does, only with
  # m_pGraphics left null. What is left out is everything that needs a renderer:
  # editor_draw.cpp draws the interface, editor_window.cpp draws the toolbar
  # and the title bar, editor_envelope.cpp draws the curve workspace, and
  # editor_textures.cpp uploads tile sheets.
  list(REMOVE_ITEM SOURCES
    "${CMAKE_CURRENT_SOURCE_DIR}/src/game/editor/editor_draw.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/game/editor/editor_window.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/game/editor/editor_envelope.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/game/editor/editor_textures.cpp")
  # ...but the editor model still needs the input queue, and CInput has no SDL
  # dependency of its own. The SVG rasterizer is pure C and has no renderer
  # either, and the client chrome is a plain hit test, so the selftest can
  # rasterize the icons and check the window furniture without a window.
  list(APPEND SOURCES "src/game/editor/editor.cpp" "src/engine/input.cpp" "src/engine/svg.cpp"
    "src/engine/chrome.cpp")
endif()

##################### executable
add_executable(${GAME_NAME} ${SOURCES})

target_include_directories(${GAME_NAME} PRIVATE src)
target_include_directories(${GAME_NAME} PRIVATE external)
target_include_directories(${GAME_NAME} PRIVATE external/libsugarx)
target_include_directories(${GAME_NAME} PRIVATE external/stb)
# nanosvg (the SVG parser and rasterizer behind the interface icons) is header
# only: one translation unit defines the implementations, the rest only names it
target_include_directories(${GAME_NAME} PRIVATE external/nanosvg/src)

# The engine headers are included by the editor model, which the headless
# build compiles too: engine/text.h needs ft2build.h and engine/painter.h needs
# SDL_Texture. Only the *include* directories are needed there, not the
# libraries, because the headless build links none of the drawing code.
if(FREETYPE_FOUND)
  target_include_directories(${GAME_NAME} PRIVATE ${FREETYPE_INCLUDE_DIRS})
endif()
if(HARFBUZZ_FOUND)
  target_include_directories(${GAME_NAME} PRIVATE ${HARFBUZZ_INCLUDE_DIRS})
endif()
if(SDL3_FOUND)
  target_include_directories(${GAME_NAME} PRIVATE ${SDL3_INCLUDE_DIRS})
endif()

if(UNITEED_GUI)
  target_link_libraries(${GAME_NAME} PRIVATE ${SDL3_LIBRARIES} ${FREETYPE_LIBRARIES} ${HARFBUZZ_LIBRARIES})
  target_compile_definitions(${GAME_NAME} PRIVATE UNITEED_GUI=1)

  # sound preview: opusfile decodes the map's .opus resources into the SDL3
  # audio stream. The headless build must not get this, it does not link SDL3
  if(OPUSFILE_FOUND)
    target_compile_definitions(${GAME_NAME} PRIVATE UNITEED_AUDIO=1)
    target_link_libraries(${GAME_NAME} PRIVATE ${OPUSFILE_LIBRARIES})
    target_include_directories(${GAME_NAME} PRIVATE ${OPUSFILE_INCLUDE_DIRS})
  else()
    message(STATUS "opusfile not found, sound preview disabled")
  endif()
else()
  # the headless build still compiles glad, it is only used for the GL types
  target_compile_definitions(${GAME_NAME} PRIVATE UNITEED_GUI=0)
endif()

target_link_libraries(${GAME_NAME} PRIVATE ${ZLIB_LIBRARIES})
target_include_directories(${GAME_NAME} PRIVATE ${ZLIB_INCLUDE_DIRS})

if(ZSTD_FOUND)
  target_compile_definitions(${GAME_NAME} PRIVATE UNITEED_ZSTD=1)
  target_link_libraries(${GAME_NAME} PRIVATE ${ZSTD_LIBRARIES})
  target_include_directories(${GAME_NAME} PRIVATE ${ZSTD_INCLUDE_DIRS})
else()
  message(STATUS "libzstd not found, datafile v5 export disabled")
endif()

if(OpenSSL_FOUND)
  # libsugarx::uuid generation (v3/v4/v5/v7) uses OpenSSL
  target_compile_definitions(${GAME_NAME} PRIVATE UNITEED_OPENSSL=1)
  target_link_libraries(${GAME_NAME} PRIVATE OpenSSL::Crypto)
endif()

# the source data directory is the default resource root, so the binary can be
# run straight from the build tree without an install step
target_compile_definitions(${GAME_NAME} PRIVATE UNITEED_DATA_DIR="${CMAKE_CURRENT_SOURCE_DIR}/data")

target_compile_options(${GAME_NAME} PRIVATE -Wall -Wextra)

if(UNITEED_WERROR)
  target_compile_options(${GAME_NAME} PRIVATE -Werror)
endif()

##################### install
install(TARGETS ${GAME_NAME} RUNTIME DESTINATION bin)
install(DIRECTORY data/ DESTINATION share/uniteed)
