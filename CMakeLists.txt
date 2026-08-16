cmake_minimum_required(VERSION 3.25)

project(efs LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(Qt6 6.8 REQUIRED COMPONENTS Core Widgets)
qt_standard_project_setup()

set(EFS_EVERYTHING_SDK_DIR "${CMAKE_CURRENT_SOURCE_DIR}/third_party/everything-sdk")
set(EFS_EVERYTHING_DLL     "${EFS_EVERYTHING_SDK_DIR}/dll/Everything64.dll")

if(NOT EXISTS "${EFS_EVERYTHING_DLL}")
    message(FATAL_ERROR "Everything64.dll not found at ${EFS_EVERYTHING_DLL}")
endif()

# Copies Everything64.dll next to <target>.exe so the runtime LoadLibraryW finds
# it without depending on PATH.
function(efs_copy_everything_dll target)
    add_custom_command(TARGET ${target} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${EFS_EVERYTHING_DLL}" "$<TARGET_FILE_DIR:${target}>"
        COMMENT "Copying Everything64.dll next to ${target}")
endfunction()

# --- efs_core -----------------------------------------------------------------
# Everything SDK boundary. Grows into core/ + backend/ from Phase 1 on.
add_library(efs_core STATIC
    src/backend/everything/EverythingApi.h
    src/backend/everything/EverythingApi.cpp
)
target_include_directories(efs_core PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/src"
    "${EFS_EVERYTHING_SDK_DIR}"
)
target_link_libraries(efs_core PUBLIC Qt6::Core)
target_compile_definitions(efs_core PUBLIC WIN32_LEAN_AND_MEAN NOMINMAX UNICODE _UNICODE)

# --- efs (GUI) ----------------------------------------------------------------
qt_add_executable(efs WIN32 src/app/main.cpp)
target_link_libraries(efs PRIVATE Qt6::Widgets)
efs_copy_everything_dll(efs)

# --- efs_spike (console, Phase 0 only) ----------------------------------------
# Walking-skeleton probe for the Everything SDK and the ext:/regex: compatibility
# question in plan section 6.2. Deleted once Phase 1 lands the real backend.
qt_add_executable(efs_spike src/spike/everything_spike.cpp)
target_link_libraries(efs_spike PRIVATE efs_core)
efs_copy_everything_dll(efs_spike)

if(MSVC)
    foreach(t efs_core efs efs_spike)
        target_compile_options(${t} PRIVATE /W4 /permissive- /utf-8 /Zc:__cplusplus)
    endforeach()
endif()
