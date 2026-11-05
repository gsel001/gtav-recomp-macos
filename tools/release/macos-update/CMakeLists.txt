cmake_minimum_required(VERSION 3.25)
project(gtav_macos_update LANGUAGES C CXX)
find_package(Python3 REQUIRED COMPONENTS Interpreter)
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_OSX_DEPLOYMENT_TARGET 15.0)
add_library(SDL3::Headers INTERFACE IMPORTED)
set_target_properties(SDL3::Headers PROPERTIES INTERFACE_INCLUDE_DIRECTORIES "${GTAV_DEPS}/rexglue-sdk/thirdparty/sdl3/include")
add_subdirectory("${GTAV_DEPS}/plume" plume)
target_link_libraries(plume PUBLIC SDL3::Headers)
add_executable(gtav_fast
 "${GTAV_REPO}/gtav/src/main.cpp"
 "${GTAV_REPO}/gtav/src/crash_handler.cpp"
 "${GTAV_REPO}/gtav/src/cutscene_audio_fix.cpp"
 "${GTAV_REPO}/gtav/src/frame_limit.cpp"
 "${GTAV_REPO}/gtav/src/wait_profile.cpp"
 "${GTAV_REPO}/gtav/src/gpu_fence_wait.cpp"
 "${GTAV_REPO}/gtav/src/native/stats.cpp"
 "${GTAV_REPO}/gtav/src/native/capture.cpp"
 "${GTAV_REPO}/gtav/src/native/d3d_probe.cpp"
 "${GTAV_REPO}/gtav/src/native/d3d_hooks.cpp"
 "${GTAV_REPO}/gtav/src/native/renderer.cpp"
 "${GTAV_REPO}/gtav/src/native/shaders.cpp"
 "${GTAV_REPO}/gtav/src/native/menu.cpp"
 "${GTAV_REPO}/gtav/src/native/backend.cpp"
 "${GTAV_REPO}/gtav/src/native/deferred_list.cpp"
 "${GTAV_REPO}/gtav/src/native/textures.cpp"
 "${GTAV_REPO}/gtav/src/native/write_watch.cpp"
 "${GTAV_REPO}/gtav/src/native/shader_compiler.cpp"
 "${GTAV_REPO}/gtav/src/native/touch_controls.cpp"
 "${GTAV_REPO}/gtav/src/game_bridge.cpp"
 "${GTAV_DEPS}/rexglue-sdk/src/ui/windowed_app_main_sdl.cpp"
 "${GTAV_DEPS}/rexglue-sdk/src/ui/rex_app.cpp"
)
target_include_directories(gtav_fast PRIVATE
 "${GTAV_REPO}/gtav"
 "${GTAV_REPO}/gtav/src"
 "${GTAV_NATIVE_HEADERS}"
 "${GTAV_DEPS}/rexglue-sdk/include"
 "${GTAV_DEPS}/rexglue-sdk/thirdparty/fmt/include"
 "${GTAV_DEPS}/rexglue-sdk/thirdparty/spdlog/include"
 "${GTAV_DEPS}/rexglue-sdk/thirdparty/imgui"
 "${GTAV_DEPS}/rexglue-sdk/thirdparty/sdl3/include"
 "${GTAV_DEPS}/rexglue-sdk/thirdparty/xxHash"
 "${GTAV_DEPS}/rexglue-sdk/thirdparty/simde"
 "${GTAV_DEPS}/rexglue-sdk/thirdparty/tomlplusplus/include"
 "${GTAV_DEPS}/gtav-recomp/gtav/src/native"
)
target_compile_definitions(gtav_fast PRIVATE GTAV_GAME_DLL=1 SPDLOG_FMT_EXTERNAL=1 REX_PLATFORM_MAC=1 REXGLUE_BUILD_CONFIG="Release")
target_compile_options(gtav_fast PRIVATE -mcpu=apple-m1 -Wno-asm-operand-widths "-ffile-prefix-map=${GTAV_REPO}=." "-ffile-prefix-map=${GTAV_DEPS}=dependencies" "-ffile-prefix-map=${GTAV_NATIVE_HEADERS}=build")
target_compile_options(plume PRIVATE "-ffile-prefix-map=${GTAV_DEPS}=dependencies")
target_link_libraries(gtav_fast PRIVATE plume "${GTAV_RUNTIME_DIR}/librexruntime.dylib" "${GTAV_RUNTIME_DIR}/librexgpu-xenos.dylib" "-framework Metal" "-framework Foundation" "-framework AppKit" "-framework QuartzCore" "-framework IOKit")
add_custom_command(TARGET gtav_fast PRE_LINK COMMAND ${CMAKE_COMMAND} -E env MACOSX_DEPLOYMENT_TARGET=15.0 ${Python3_EXECUTABLE} "${GTAV_REPO}/tools/game_dll/gen_interface.py" ${GTAV_LLVM_BIN} "${CMAKE_BINARY_DIR}/CMakeFiles/gtav_fast.dir" "${CMAKE_BINARY_DIR}/game_interface" "${GTAV_RUNTIME_DIR}/librexruntime.dylib" /usr/bin/clang++)
target_link_options(gtav_fast PRIVATE "${CMAKE_BINARY_DIR}/game_interface/stub/gtav_game.dylib" "LINKER:-export_dynamic")

set_target_properties(gtav_fast PROPERTIES BUILD_WITH_INSTALL_RPATH ON INSTALL_RPATH "@executable_path")
