cmake_minimum_required(VERSION 3.16)
list(APPEND CMAKE_MODULE_PATH ${CMAKE_SOURCE_DIR}/cmake)

if(APPLE)
  if(CMAKE_OSX_ARCHITECTURES MATCHES "arm64")
    # ARM Macs start at OS 11.
    set(CMAKE_OSX_DEPLOYMENT_TARGET "11.0" CACHE STRING "Minimum OS X deployment version")
  else()
    # SDL2 requires at least this version for everything to work properly.
    set(CMAKE_OSX_DEPLOYMENT_TARGET "10.9" CACHE STRING "Minimum OS X deployment version")
  endif()
endif()

if(VITA)
  if(NOT DEFINED CMAKE_TOOLCHAIN_FILE)
    if(DEFINED ENV{VITASDK})
      set(CMAKE_TOOLCHAIN_FILE "$ENV{VITASDK}/share/vita.toolchain.cmake" CACHE PATH "toolchain file")
    else()
      message(FATAL_ERROR "Please define VITASDK to point to your SDK path!")
    endif()
  endif()
endif()

project(pd C CXX)

if(VITA)
  include("${VITASDK}/share/vita.cmake" REQUIRED)
  set(VITA_APP_NAME "Perfect Dark Experimental")
  set(VITA_TITLEID  "PRFCDKEXP")
  # Trophy packs use a separate communication ID (CommID) which must match:
  # - The folder name under sce_sys/trophy/<COMMID>_00/
  # - The CommID passed to sceNpTrophyCreateContext
  # - The CommID baked into the TROPHY.TRP itself
  set(VITA_TROPHY_COMM_ID "PDXP00001")
  set(VITA_VERSION  "01.00")
endif()

if(NOT LAUNCHER)
include(TargetArch)
include(FindPkgConfig)
include(AssetUtils)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_EXTENSIONS OFF)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# On first build set the build type to RelWithDebInfo if it's not already set
if (NOT EXISTS ${CMAKE_BINARY_DIR}/CMakeCache.txt)
  if (NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE "RelWithDebInfo" CACHE STRING "" FORCE)
  endif()
endif()

# Options

set(ROMID "ntsc-final" CACHE STRING "Which ROM to build an executable for: ntsc-final, pal-final or jpn-final")
option(FORCE_CROSSCOMPILE "Force cross compiling" NO)

# Determine target arch

target_architecture(TARGET_ARCH)
if(TARGET_ARCH STREQUAL "i386")
  # for historical reasons
  set(TARGET_ARCH "i686")
endif()

# TODO
set(TARGET_IS_BIG_ENDIAN FALSE)

if((TARGET_ARCH MATCHES "unknown") OR (TARGET_ARCH STREQUAL ""))
  message(FATAL_ERROR "Unknown target arch")
elseif(TARGET_ARCH MATCHES "64")
  set(TARGET_IS_64BIT TRUE)
else()
  set(TARGET_IS_64BIT FALSE)
endif()

message(STATUS "Target arch: ${TARGET_ARCH}")

# Determine target OS and platform identifier ($arch-$os)

if(WIN32)
  # Windows
  set(TARGET_OS "windows")
elseif(APPLE)
  # OSX
  set(TARGET_OS "osx")
elseif(NINTENDO_SWITCH)
  # Switch
  set(TARGET_OS "nswitch")
elseif(UNIX)
  # Generic *nix; assume Linux
  set(TARGET_OS "linux")
elseif(VITA)
  # PlayStation Vita
  set(TARGET_OS "vita")
else()
  # ???
  message(FATAL_ERROR "Unknown target OS or compiler")
endif()

set(TARGET_PLATFORM "${TARGET_ARCH}-${TARGET_OS}")

message(STATUS "Target platform: ${TARGET_PLATFORM}")

# Calculate version info and binary name

set(VERSION_IS_PAL 0)

if(ROMID STREQUAL "ntsc-final")
  set(VERSION 2)
  set(BIN_REGION "")
elseif(ROMID STREQUAL "pal-final")
  set(VERSION 4)
  set(BIN_REGION ".pal")
  set(VERSION_IS_PAL 1)
elseif(ROMID STREQUAL "jpn-final")
  set(VERSION 5)
  set(BIN_REGION ".jpn")
else()
  message(FATAL_ERROR "Unknown ROM ID: ${ROMID}")
endif()

# git commit hash
execute_process(
  COMMAND git rev-parse --short HEAD
  WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
  OUTPUT_VARIABLE VERSION_HASH
  OUTPUT_STRIP_TRAILING_WHITESPACE
)

# git branch name
execute_process(
  COMMAND git rev-parse --abbrev-ref HEAD
  WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
  OUTPUT_VARIABLE VERSION_BRANCH
  OUTPUT_STRIP_TRAILING_WHITESPACE
)

set(VERSION_TARGET "${TARGET_PLATFORM}")
set(VERSION_ARCH "${TARGET_ARCH}")
set(VERSION_ROMID "${ROMID}")
set(VERSION_BUILD "${CMAKE_BUILD_TYPE}")

if((VERSION_BRANCH STREQUAL "") OR (VERSION_BRANCH STREQUAL "HEAD"))
  set(VERSION_BRANCH "port-custom")
endif()

message(STATUS "Version: ${VERSION_ROMID}, hash ${VERSION_HASH}, branch ${VERSION_BRANCH}")

configure_file("${CMAKE_SOURCE_DIR}/port/include/versioninfo.h.in" "${CMAKE_BINARY_DIR}/port/include/versioninfo.h" @ONLY)

set(ASSET_DIR "src/assets/${ROMID}")
set(GENERATED_DIR "src/generated/${ROMID}")

set(ENV{ROMID} ${ROMID})

# Build folder postfix

set(BUILD_ID "${VERSION_ROMID}-${VERSION_TARGET}")

# Compiler defs

add_definitions(
  -DMATCHING=0
  -DPIRACYCHECKS=0
  -DROM_SIZE=32
  -DAVOID_UB=1
  -D_LANGUAGE_C=1
  -DPAL=${VERSION_IS_PAL}
  -DVERSION=${VERSION}
)

if(TARGET_IS_64BIT)
  add_definitions(-DPLATFORM_64BIT=1)
endif()

# Compiler flags

# Include directories
include_directories("${CMAKE_SOURCE_DIR}/include")
include_directories("${CMAKE_SOURCE_DIR}/include/PR")
include_directories("${CMAKE_SOURCE_DIR}/src/include")
include_directories("${CMAKE_BINARY_DIR}/src/generated/${ROMID}")
include_directories("${CMAKE_SOURCE_DIR}/src/lib/ultra/audio")
include_directories("${CMAKE_SOURCE_DIR}/port/include")
include_directories("${CMAKE_BINARY_DIR}/port/include")

# Set Release optimization to -Og until I fix the -O2 issues
if (VITA)
  add_compile_options(-O3
    -fno-optimize-sibling-calls
    -fno-peephole2
    -fno-schedule-insns2
  )
else()
  if(NOT (CMAKE_BUILD_TYPE STREQUAL "Debug"))
    add_compile_options(-Og)
  endif()
endif()


# Common CFLAGS
add_compile_options(
  -fno-strict-aliasing
  -funsigned-char
  -fwrapv
  -include "versioninfo.h"
)

# Common warning flags
add_compile_options(
  -Wall
  -Wno-maybe-uninitialized
  -Wno-address
  -Wno-int-in-bool-context
  -Wno-misleading-indentation
  -Wno-missing-braces
  -Wno-multichar
  -Wno-tautological-compare
  -Wno-unused-but-set-variable
  -Wno-unused-value
  -Wno-unused-variable
  -fdiagnostics-color=always
)

# C specific flags
add_compile_options($<$<COMPILE_LANGUAGE:C>:-Wno-pointer-sign>)

# C++ specific flags
add_compile_options($<$<COMPILE_LANGUAGE:CXX>:-Wno-unused-function>)

# Platform-specific flags
if (APPLE)
  # Make it look for libs in the executable folder manually since CMake's rpath system is wack
  set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -rpath @executable_path")
else()
  # This warning is not recognized by XCode/Clang for some reason.
  add_compile_options(-Wno-format-truncation)
endif()

# Dependencies

set(LIBS )

# SDL2
if(VITA)
  set(SDL2_LIBRARY "SDL2")
  set(SDL2_INCLUDE_DIR "${VITASDK}/arm-vita-eabi/include/SDL2")
else()
  find_package(SDL2 REQUIRED)
  if(NINTENDO_SWITCH)
    if(NOT SDL2_LIBRARY OR NOT SDL2_INCLUDE_DIR)
      # sometimes FindSDL2 just wants us to suffer and does not set these
      set(SDL2_LIBRARY "SDL2")
      set(SDL2_INCLUDE_DIR "${DEVKITPRO}/portlibs/switch/include/SDL2")
    endif()
  endif()
endif()
include_directories(SYSTEM "${SDL2_INCLUDE_DIR}")
list(APPEND LIBS "${SDL2_LIBRARY}")

# ZLib
find_package(ZLIB REQUIRED)
if(ZLIB_INCLUDE_DIR)
  include_directories(SYSTEM "${ZLIB_INCLUDE_DIR}")
endif()
list(APPEND LIBS "${ZLIB_LIBRARY}")

# GL
if(NOT GL_LIBRARY)
  if(WIN32)
    set(GL_LIBRARY opengl32)
  elseif(NINTENDO_SWITCH)
    set(GL_LIBRARY EGL glapi drm_nouveau)
  elseif(APPLE)
    set(GL_LIBRARY "-framework OpenGL")
  elseif(VITA)
    set(GL_LIBRARY vitaGL vitashark SceShaccCgExt SceShaccCg_stub taihen_stub mathneon SceGxm_stub SceDisplay_stub SceAppMgr_stub)
  else()
    set(GL_LIBRARY GL)
  endif()
endif()
list(APPEND LIBS "${GL_LIBRARY}")

# Platform-specific extra libs
if(NOT EXTRA_LIBRARIES)
  if(WIN32)
    set(EXTRA_LIBRARIES dbghelp -static-libstdc++)
  elseif(NINTENDO_SWITCH)
    set(EXTRA_LIBRARIES stdc++ pthread)
  elseif(VITA)
    set(EXTRA_LIBRARIES stdc++ pthread SceAppUtil_stub SceMotion_stub SceCtrl_stub SceTouch_stub SceHid_stub SceIme_stub SceCommonDialog_stub SceLibKernel_stub SceKernelDmacMgr_stub SceAudioIn_stub SceAudio_stub SceSysmodule_stub SceNpTrophy_stub ScePower_stub)
  else()
    set(EXTRA_LIBRARIES stdc++ m dl)
  endif()
endif()
list(APPEND LIBS "${EXTRA_LIBRARIES}")

# Sources

file(GLOB_RECURSE SRC_PORT "${CMAKE_SOURCE_DIR}/port/*.c")
file(GLOB_RECURSE SRC_PORT_CXX "${CMAKE_SOURCE_DIR}/port/*.cpp")

file(GLOB_RECURSE SRC_GAME "${CMAKE_SOURCE_DIR}/src/game/*.c")

file(GLOB SRC_LIB_MP3 "${CMAKE_SOURCE_DIR}/src/lib/mp3/*.c")
file(GLOB SRC_LIB_NAUDIO "${CMAKE_SOURCE_DIR}/src/lib/naudio/*.c")
file(GLOB SRC_LIB_ULTRA_AUDIO "${CMAKE_SOURCE_DIR}/src/lib/ultra/audio/*.c")
file(GLOB SRC_LIB_GU "${CMAKE_SOURCE_DIR}/src/lib/ultra/gu/*.c")
set(SRC_LIB
  src/lib/ultra/io/vimodentsclan1.c
  src/lib/ultra/io/vimodepallan1.c
  src/lib/ultra/io/vimodempallan1.c
  src/lib/ultra/io/vitbl.c
  src/lib/ailist.c
  src/lib/anim.c
  src/lib/args.c
  src/lib/audiodma.c
  src/lib/audiomgr.c
  src/lib/base.c
  src/lib/collision.c
  src/lib/debughud.c
  src/lib/dma.c
  src/lib/fault.c
  src/lib/joy.c
  src/lib/lib_2f490_c.c
  src/lib/lib_2fc60.c
  src/lib/lib_3e3e0.c
  src/lib/lib_04f60nb.c
  src/lib/lib_17ce0.c
  src/lib/lib_034d0.c
  src/lib/lib_39c80.c
  src/lib/lib_15850.c
  src/lib/mema.c
  src/lib/memp.c
  src/lib/model.c
  src/lib/modelasm_c.c
  src/lib/mp3.c
  src/lib/mtx_c.c
  src/lib/mtx.c
  src/lib/music.c
  src/lib/path.c
  src/lib/profile.c
  src/lib/rdp.c
  src/lib/rmon.c
  src/lib/rng_c.c
  src/lib/rzip_c.c
  src/lib/snd.c
  src/lib/speaker.c
  src/lib/varsinit.c
  src/lib/vi.c
  src/textureconfig.c
)
list(PREPEND SRC_LIB "${CMAKE_SOURCE_DIR}")

if (VITA)
set(SRC_VITA
  vita/trophies.c
)

set(SRC
  ${SRC_GAME}
  ${SRC_PORT}
  ${SRC_PORT_CXX}
  ${SRC_LIB_MP3}
  ${SRC_LIB_NAUDIO}
  ${SRC_LIB_ULTRA_AUDIO}
  ${SRC_LIB_GU}
  ${SRC_LIB}
  ${SRC_VITA}
)
else()
set(SRC
  ${SRC_GAME}
  ${SRC_PORT}
  ${SRC_PORT_CXX}
  ${SRC_LIB_MP3}
  ${SRC_LIB_NAUDIO}
  ${SRC_LIB_ULTRA_AUDIO}
  ${SRC_LIB_GU}
  ${SRC_LIB}
)
endif()

if(WIN32)
  list(APPEND SRC "${CMAKE_SOURCE_DIR}/dist/windows/icon.rc")
endif()

# Asset files

generate_asset_headers("lang/" "${CMAKE_SOURCE_DIR}/tools/assetmgr/mklang" "en" JSON_HEADERS)
generate_asset_headers("pads/" "${CMAKE_SOURCE_DIR}/tools/assetmgr/mkpads" "" JSON_HEADERS)
generate_asset_headers("tiles/" "${CMAKE_SOURCE_DIR}/tools/assetmgr/mktiles" "" JSON_HEADERS)
generate_asset_headers("animations.json" "${CMAKE_SOURCE_DIR}/tools/assetmgr/mkanims" "" JSON_HEADERS)
generate_asset_headers("sequences.json" "${CMAKE_SOURCE_DIR}/tools/assetmgr/mksequences" "" JSON_HEADERS)
generate_asset_headers("textures.json" "${CMAKE_SOURCE_DIR}/tools/assetmgr/mktextures" "" JSON_HEADERS)

add_custom_target(pd_headers ALL DEPENDS ${JSON_HEADERS})

# Game target

set(BIN_NAME "pd${BIN_REGION}.${TARGET_ARCH}")
set(EXP_BIN_NAME "pdexp${BIN_REGION}.${TARGET_ARCH}")
add_executable(pd ${SRC})
add_dependencies(pd pd_headers)
target_link_libraries(pd ${LIBS})
if(NOT VITA)
  set_target_properties(pd PROPERTIES OUTPUT_NAME "${BIN_NAME}")
endif()

if(VITA)
  target_compile_definitions(pd PRIVATE PD_VITA_COMM_ID="${VITA_TROPHY_COMM_ID}")
endif()

# Provide a `pdexp` build target alias so Vita packaging rules that expect
# a `pdexp` target have something to depend on (it forwards to `pd`).
add_custom_target(pdexp DEPENDS pd)

# Optional helper: create a script and custom target to run the build and
# capture stdout/stderr into a dedicated log file `build.log` in the build dir.
option(ENABLE_BUILD_LOG "Create helper target 'build-with-log' to capture build output into a file" ON)
if(ENABLE_BUILD_LOG)
  set(_build_log_script "${CMAKE_BINARY_DIR}/build_with_log.sh")
  file(WRITE "${_build_log_script}"
    "#!/usr/bin/env bash\n"
    "set -euo pipefail\n"
    "BUILD_DIR=\"${CMAKE_BINARY_DIR}\"\n"
    "LOG_FILE=\"${CMAKE_BINARY_DIR}/build.log\"\n"
    "JOBS=\"${CMAKE_BUILD_PARALLEL_LEVEL}\"\n"
    "if [ -z \"\$JOBS\" ]; then\n"
    "  if command -v nproc >/dev/null 2>&1; then JOBS=$(nproc); else JOBS=1; fi\n"
    "fi\n"
    "cd \"${CMAKE_BINARY_DIR}\"\n"
    "cmake --build . -- -j\$JOBS 2>&1 | tee \"\$LOG_FILE\"\n"
    "exit \${PIPESTATUS[0]}\n"
  )
  file(CHMOD "${_build_log_script}" PERMISSIONS OWNER_READ OWNER_WRITE OWNER_EXECUTE GROUP_READ GROUP_EXECUTE WORLD_READ WORLD_EXECUTE)
  add_custom_target(build-with-log COMMAND bash "${_build_log_script}" WORKING_DIRECTORY "${CMAKE_BINARY_DIR}")
endif()
if(WIN32)
  set_target_properties(pd PROPERTIES WIN32_EXECUTABLE TRUE)
endif()

# Additional commands for specific platforms.

if(NINTENDO_SWITCH)
  add_custom_command(TARGET pd POST_BUILD
    COMMAND nacptool --create "Perfect Dark" "fgsfds and contributors" "${VERSION_ROMID}/${VERSION_BRANCH}/${VERSION_HASH}" "${BIN_NAME}.nacp"
    COMMAND aarch64-none-elf-strip -o ${CMAKE_BINARY_DIR}/${BIN_NAME}.stripped.elf ${CMAKE_BINARY_DIR}/${BIN_NAME}.elf
    COMMAND elf2nro ${CMAKE_BINARY_DIR}/${BIN_NAME}.stripped.elf ${CMAKE_BINARY_DIR}/${BIN_NAME}.nro --icon=${CMAKE_SOURCE_DIR}/dist/nswitch/icon.jpg --nacp=${BIN_NAME}.nacp
  )
endif()

if(VITA)
  # create a distinct experimental self filename so launcher packaging can find it
  set(EXP_BIN_NAME "pdexp${BIN_REGION}.${TARGET_ARCH}")
  vita_create_self(${EXP_BIN_NAME}.self pd UNSAFE)
endif()

else() # Launcher
  set(VITA_MKSFOEX_FLAGS "-d ATTRIBUTE2=12 -s NP_COMMUNICATION_ID=${VITA_TROPHY_COMM_ID}")
    vita_create_vpk(pdexp.armv7.vpk ${VITA_TITLEID} ${CMAKE_SOURCE_DIR}/vita_release/ntsc/pdexp.armv7.self
    VERSION ${VITA_VERSION}
    NAME ${VITA_APP_NAME}
    FILE ${CMAKE_SOURCE_DIR}/livearea/icon0.png sce_sys/icon0.png
         ${CMAKE_SOURCE_DIR}/livearea/pic0.png sce_sys/pic0.png
         ${CMAKE_SOURCE_DIR}/livearea/bg0.png sce_sys/livearea/contents/bg0.png
         ${CMAKE_SOURCE_DIR}/livearea/joanna.png sce_sys/livearea/contents/joanna.png
         ${CMAKE_SOURCE_DIR}/livearea/template.xml sce_sys/livearea/contents/template.xml
      ${CMAKE_SOURCE_DIR}/livearea/pd.ini pd.ini
      ${CMAKE_SOURCE_DIR}/vita_release/jap/pdexp.jpn.armv7.self jap.self
      ${CMAKE_SOURCE_DIR}/vita_release/pal/pdexp.pal.armv7.self pal.self
          ${CMAKE_SOURCE_DIR}/livearea/TROPHY.TRP sce_sys/trophy/${VITA_TROPHY_COMM_ID}_00/TROPHY.TRP
  )
endif()
