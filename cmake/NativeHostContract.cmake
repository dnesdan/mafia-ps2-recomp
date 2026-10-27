# Exercise the actual application translation unit and macOS bundle with an
# authored, test-only ELF. Nothing here is linked into the game executable.
function(mafia_add_native_host_contract)
    set(contract_dir "${CMAKE_CURRENT_BINARY_DIR}/synthetic-native-host")
    set(contract_script "${CMAKE_CURRENT_SOURCE_DIR}/tests/cpp/native_host_contract.py")
    file(MAKE_DIRECTORY "${contract_dir}")
    execute_process(
        COMMAND "${Python3_EXECUTABLE}" "${contract_script}"
            --write-fixture "${contract_dir}/synthetic.elf"
        RESULT_VARIABLE fixture_status ERROR_VARIABLE fixture_error)
    if(NOT fixture_status EQUAL 0)
        message(FATAL_ERROR "Cannot create synthetic native-host fixture: ${fixture_error}")
    endif()
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${contract_script}")
    file(SHA256 "${contract_dir}/synthetic.elf" MAFIA_ELF_SHA256)
    set(MAFIA_ELF_ENTRY 1048576)
    set(MAFIA_PROFILE_ID "synthetic-native-host-contract")
    if(MAFIA_ENABLE_FFMPEG)
        set(MAFIA_FFMPEG_BOOL true)
    else()
        set(MAFIA_FFMPEG_BOOL false)
    endif()
    configure_file("${CMAKE_CURRENT_SOURCE_DIR}/cmake/build_profile.hpp.in"
        "${contract_dir}/mafia_build_profile.hpp" @ONLY)
    add_executable(MafiaHostContract src/native_main.cpp tests/cpp/native_host_fixture.cpp)
    target_include_directories(MafiaHostContract PRIVATE "${contract_dir}")
    target_link_libraries(MafiaHostContract PRIVATE mafia_host_core ps2_runtime)
    target_compile_options(MafiaHostContract PRIVATE -fno-fast-math -ffp-contract=off)
    if(APPLE)
        set_target_properties(MafiaHostContract PROPERTIES
            MACOSX_BUNDLE TRUE
            MACOSX_BUNDLE_GUI_IDENTIFIER "cz.dnesdan.mafia-native-host-contract"
            MACOSX_BUNDLE_BUNDLE_NAME "MafiaHostContract"
            MACOSX_BUNDLE_SHORT_VERSION_STRING "0.1.0"
            MACOSX_BUNDLE_BUNDLE_VERSION "1")
    endif()
    add_test(NAME mafia_native_host_contract
        COMMAND "${Python3_EXECUTABLE}" "${contract_script}"
            --executable "$<TARGET_FILE:MafiaHostContract>"
            --fixture "${contract_dir}/synthetic.elf"
            --work-dir "${contract_dir}/execution-$<CONFIG>")
    set_tests_properties(mafia_native_host_contract PROPERTIES TIMEOUT 30)
endfunction()
