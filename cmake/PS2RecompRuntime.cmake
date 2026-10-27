if(NOT EXISTS "${MAFIA_PS2RECOMP_SOURCE_DIR}/ps2xRuntime/CMakeLists.txt")
    message(FATAL_ERROR "Set MAFIA_PS2RECOMP_SOURCE_DIR to the pinned checkout")
endif()
find_package(Git REQUIRED)
execute_process(COMMAND "${GIT_EXECUTABLE}" -C "${MAFIA_PS2RECOMP_SOURCE_DIR}" rev-parse HEAD
    RESULT_VARIABLE revision_status OUTPUT_VARIABLE actual_revision OUTPUT_STRIP_TRAILING_WHITESPACE)
if(NOT revision_status EQUAL 0 OR NOT actual_revision STREQUAL MAFIA_EXPECTED_UPSTREAM)
    message(FATAL_ERROR "PS2Recomp checkout must be ${MAFIA_EXPECTED_UPSTREAM}; got ${actual_revision}")
endif()
find_package(Python3 3.11 REQUIRED COMPONENTS Interpreter)
# CMake files are executable input too. Validate the entire patched tree before
# add_subdirectory or FetchContent can evaluate anything from that checkout.
execute_process(
    COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tools/verify_upstream.py"
        "${MAFIA_PS2RECOMP_SOURCE_DIR}"
    RESULT_VARIABLE source_verification_status
    OUTPUT_VARIABLE source_verification_output
    ERROR_VARIABLE source_verification_error)
if(NOT source_verification_status EQUAL 0)
    message(FATAL_ERROR "PS2Recomp source verification failed before configuration:\n${source_verification_output}${source_verification_error}")
endif()
message(STATUS "Verified the exact pinned and patched PS2Recomp source tree")
add_custom_target(mafia_verify_runtime_source
    COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tools/verify_upstream.py"
        "${MAFIA_PS2RECOMP_SOURCE_DIR}"
    VERBATIM)

# Add only the runtime and IOP targets. Upstream's top-level analyzer/recompiler
# CMake uses CMAKE_SOURCE_DIR and its stock runner assumes in-tree game sources.
# EXCLUDE_FROM_ALL keeps that runner out of our executable and test builds.
set(PS2X_ENABLE_DEBUG_UI OFF CACHE BOOL "" FORCE)
set(PS2X_ENABLE_RUNNER_PCH OFF CACHE BOOL "" FORCE)
set(PS2X_ENABLE_RUNNER_UNITY_BUILD OFF CACHE BOOL "" FORCE)
set(PS2X_ENABLE_SCCACHE OFF CACHE BOOL "" FORCE)
set(PS2X_ENABLE_AGRESSIVE_LOGS OFF CACHE BOOL "" FORCE)
set(PS2X_ENABLE_FFMPEG "${MAFIA_ENABLE_FFMPEG}" CACHE BOOL "" FORCE)
set(PS2X_STRICT_RETURN_DIAGNOSTICS ON CACHE BOOL "" FORCE)
set(PS2X_IOP_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(BUILD_GAMES OFF CACHE BOOL "" FORCE)
set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)

include(FetchContent)
# First declaration wins over the upstream tag-only declaration.
FetchContent_Declare(raylib
    GIT_REPOSITORY https://github.com/raysan5/raylib.git
    GIT_TAG c1ab645ca298a2801097931d1079b10ff7eb9df8)
if(CMAKE_SYSTEM_PROCESSOR MATCHES "^(arm64|aarch64|ARM64)$" OR
   (APPLE AND CMAKE_OSX_ARCHITECTURES STREQUAL "arm64"))
    FetchContent_Declare(sse2neon
        GIT_REPOSITORY https://github.com/DLTcollab/sse2neon.git
        GIT_TAG 92f6de174717aef09033ad21568d5bb9e5470404)
    FetchContent_MakeAvailable(sse2neon)
    add_compile_definitions(USE_SSE2NEON)
    include_directories(SYSTEM "${sse2neon_SOURCE_DIR}")
elseif(CMAKE_SYSTEM_PROCESSOR MATCHES "^(x86_64|AMD64|amd64|i.86)$")
    add_compile_options(-msse4.1)
else()
    message(FATAL_ERROR "PS2Recomp host requires ARM64/NEON or x86/SSE4.1")
endif()
add_subdirectory("${MAFIA_PS2RECOMP_SOURCE_DIR}/ps2xIOP" upstream/iop EXCLUDE_FROM_ALL)
add_subdirectory("${MAFIA_PS2RECOMP_SOURCE_DIR}/ps2xRuntime" upstream/runtime EXCLUDE_FROM_ALL)
add_dependencies(ps2_iop mafia_verify_runtime_source)
add_dependencies(ps2_runtime mafia_verify_runtime_source)
target_compile_options(ps2_runtime PRIVATE -fno-fast-math -ffp-contract=off)
