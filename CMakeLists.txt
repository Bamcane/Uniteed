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

option(UNITEED_GUI "build the graphical editor (needs glfw, opengl, nanovg)" ON)
option(UNITEED_WERROR "treat warnings as errors" OFF)

##################### system libraries, kept as light as possible
find_package(PkgConfig REQUIRED)
pkg_check_modules(GLFW glfw3)
pkg_check_modules(ZLIB REQUIRED zlib)
pkg_check_modules(ZSTD libzstd)
find_package(OpenSSL)

if(UNITEED_GUI AND NOT GLFW_FOUND)
  message(STATUS "glfw3 not found, falling back to the headless build")
  set(UNITEED_GUI OFF)
endif()

if(UNITEED_GUI)
  find_package(OpenGL REQUIRED)
endif()

##################### vendored / submodule libraries
# libsugarx is a git submodule, header only
if(NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/external/libsugarx/sugar_types.h")
  message(FATAL_ERROR "the libsugarx submodule is missing, run: git submodule update --init --recursive")
endif()

##################### sources
file(GLOB_RECURSE BASE_SOURCES CONFIGURE_DEPENDS "src/base/*.cpp"   "src/base/*.h")
file(GLOB_RECURSE CONFIG_SOURCES CONFIGURE_DEPENDS "src/config/*.cpp" "src/config/*.h")
file(GLOB_RECURSE GAME_SOURCES CONFIGURE_DEPENDS "src/game/*.cpp"   "src/game/*.h")
set(SOURCES ${BASE_SOURCES} ${CONFIG_SOURCES} ${GAME_SOURCES})
# the entry point is always part of the build, the window code is optional
list(APPEND SOURCES "src/engine/main.cpp")

if(UNITEED_GUI)
  # the editor window pulls in the engine layer, the headless build only needs
  # the command line
  file(GLOB ENGINE_SOURCES CONFIGURE_DEPENDS "src/engine/*.cpp" "src/engine/*.h")
  list(REMOVE_ITEM ENGINE_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/src/engine/main.cpp")
  list(APPEND SOURCES ${ENGINE_SOURCES} "src/game/editor/editor.cpp" "src/game/editor/editor_draw.cpp")
else()
  list(REMOVE_ITEM SOURCES
    "${CMAKE_CURRENT_SOURCE_DIR}/src/game/editor/editor.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/game/editor/editor_draw.cpp")
endif()

if(UNITEED_GUI)
  # nanovg is a git submodule and built as an object library
  if(NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/external/nanovg/src/nanovg.c")
    message(FATAL_ERROR "the nanovg submodule is missing, run: git submodule update --init --recursive")
  endif()
  add_library(nanovg OBJECT "external/nanovg/src/nanovg.c")
  target_include_directories(nanovg PRIVATE external/nanovg/src)
  target_compile_options(nanovg PRIVATE -w)

  # glad comes pre-generated from SandboxCmd (regenerating needs python and the
  # Khronos registry, so the generated pair is carried along instead)
  add_library(glad OBJECT "external/glad/gl.c")
  target_include_directories(glad PRIVATE external/glad)
  target_compile_options(glad PRIVATE -w)
endif()

##################### executable
add_executable(${GAME_NAME} ${SOURCES})

target_include_directories(${GAME_NAME} PRIVATE src)
target_include_directories(${GAME_NAME} PRIVATE external)
target_include_directories(${GAME_NAME} PRIVATE external/libsugarx)
target_include_directories(${GAME_NAME} PRIVATE external/nanovg/src)
target_include_directories(${GAME_NAME} PRIVATE external/glad)

if(UNITEED_GUI)
  target_sources(${GAME_NAME} PRIVATE $<TARGET_OBJECTS:nanovg> $<TARGET_OBJECTS:glad>)
  target_include_directories(${GAME_NAME} PRIVATE ${GLFW_INCLUDE_DIRS})
  target_link_libraries(${GAME_NAME} PRIVATE ${GLFW_LIBRARIES} OpenGL::GL)
  target_compile_definitions(${GAME_NAME} PRIVATE UNITEED_GUI=1)
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
