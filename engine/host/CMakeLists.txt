# spektralab-host: the engine as a child process (desktop/HOST-PROTOCOL.md).
add_executable(spektralab-host
    main.cpp
    image_io.cpp
    output.cpp
    platform.cpp
    stb_impl.cpp
    tiff_ifd.cpp
    white_balance.cpp
)
target_compile_features(spektralab-host PRIVATE cxx_std_20)
target_include_directories(spektralab-host PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}"
    "${CMAKE_CURRENT_SOURCE_DIR}/../include"
    "${CMAKE_CURRENT_SOURCE_DIR}/../src"
    "${CMAKE_CURRENT_SOURCE_DIR}/../third_party/stb")
target_link_libraries(spektralab-host PRIVATE spk_image_io spk_libraw spektralab_engine spektralab_core)
if(BUILD_TESTING)
    add_executable(spk_host_white_balance_test white_balance_test.cpp white_balance.cpp)
    target_compile_features(spk_host_white_balance_test PRIVATE cxx_std_20)
    target_include_directories(spk_host_white_balance_test PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
    target_link_libraries(spk_host_white_balance_test PRIVATE spk_libraw)
    add_test(NAME spk_host_white_balance COMMAND spk_host_white_balance_test)
    find_package(Python3 COMPONENTS Interpreter QUIET)
    if(Python3_Interpreter_FOUND)
        add_test(NAME spk_host_white_balance_pick
            COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/../tests/host_white_balance_pick.py"
                --host "$<TARGET_FILE:spektralab-host>" --resources "${CMAKE_CURRENT_BINARY_DIR}/../resources")
    endif()
endif()
if(DEFINED SPEKTRALAB_HOST_VERSION)
    target_compile_definitions(spektralab-host PRIVATE SPEKTRALAB_HOST_VERSION="${SPEKTRALAB_HOST_VERSION}")
endif()
if(WIN32)
    target_link_libraries(spektralab-host PRIVATE ws2_32 shell32)
endif()
if(SPEKTRALAB_STATIC_RUNTIME AND CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
    target_link_options(spektralab-host PRIVATE -static-libstdc++ -static-libgcc)
    if(MINGW)
        target_link_options(spektralab-host PRIVATE -static)
    endif()
endif()
