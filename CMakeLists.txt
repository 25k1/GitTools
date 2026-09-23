cmake_minimum_required(VERSION 3.25)
project(gittools LANGUAGES CXX)

if(NOT WIN32)
    message(FATAL_ERROR "gittools targets Windows only.")
endif()

enable_language(RC)

set(GITTOOLS_VERSION "0.4" CACHE STRING "Version to stamp into the binary, e.g. 1.2.3 or v1.2.3-rc1")

string(REGEX REPLACE "^[vV]" "" GITTOOLS_VERSION_STR "${GITTOOLS_VERSION}")
string(REGEX MATCH "^[0-9]+(\\.[0-9]+)*" GITTOOLS_VERSION_NUMERIC
       "${GITTOOLS_VERSION_STR}")
if(NOT GITTOOLS_VERSION_NUMERIC)
    message(FATAL_ERROR
        "GITTOOLS_VERSION '${GITTOOLS_VERSION}' must look like 1.2.3 or v1.2.3")
endif()
string(REPLACE "." ";" GITTOOLS_VERSION_PARTS "${GITTOOLS_VERSION_NUMERIC}")
list(LENGTH GITTOOLS_VERSION_PARTS GITTOOLS_VERSION_COUNT)
if(GITTOOLS_VERSION_COUNT GREATER 4)
    message(FATAL_ERROR
        "GITTOOLS_VERSION '${GITTOOLS_VERSION}' has more than four numbers")
endif()
while(GITTOOLS_VERSION_COUNT LESS 4)
    list(APPEND GITTOOLS_VERSION_PARTS 0)
    math(EXPR GITTOOLS_VERSION_COUNT "${GITTOOLS_VERSION_COUNT} + 1")
endwhile()
foreach(part IN LISTS GITTOOLS_VERSION_PARTS)
    if(part GREATER 65535)
        message(FATAL_ERROR
            "GITTOOLS_VERSION '${GITTOOLS_VERSION}': each number must be <= 65535")
    endif()
endforeach()
list(JOIN GITTOOLS_VERSION_PARTS "," GITTOOLS_VERSION_FIELDS)
list(JOIN GITTOOLS_VERSION_PARTS "." GITTOOLS_VERSION_FULL)
message(STATUS "gittools: version ${GITTOOLS_VERSION_STR} (${GITTOOLS_VERSION_FULL})")

set(GITTOOLS_GENERATED_DIR "${CMAKE_CURRENT_BINARY_DIR}/generated")
set(GITTOOLS_RESOURCE_DIR  "${CMAKE_CURRENT_SOURCE_DIR}/resources")
configure_file(src/Version.h.in       "${GITTOOLS_GENERATED_DIR}/Version.h"    @ONLY)
configure_file(resources/app.manifest.in "${GITTOOLS_GENERATED_DIR}/app.manifest" @ONLY)
configure_file(resources/app.rc.in    "${GITTOOLS_GENERATED_DIR}/app.rc"       @ONLY)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(GITTOOLS_LTO "Link-time optimisation in optimised builds" ON)
option(GITTOOLS_EXCEPTIONS "Build with C++ exceptions enabled" OFF)
option(GITTOOLS_STATIC_CRT "Statically link the MSVC C runtime" ON)
if(GITTOOLS_STATIC_CRT)
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
else()
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL")
endif()

if(MSVC AND CMAKE_CXX_COMPILER_ID MATCHES "Clang")
    set(GITTOOLS_CLANG_CL TRUE)
else()
    set(GITTOOLS_CLANG_CL FALSE)
endif()

if(MSVC)
    set(GITTOOLS_CXX "$<COMPILE_LANGUAGE:CXX>")

    add_compile_options(
        "$<${GITTOOLS_CXX}:/W4>"
        "$<${GITTOOLS_CXX}:/utf-8>"
        "$<${GITTOOLS_CXX}:/GR->"
    )

    if(NOT GITTOOLS_CLANG_CL)
        add_compile_options(
            "$<${GITTOOLS_CXX}:/permissive->"
            "$<${GITTOOLS_CXX}:/Zc:__cplusplus>"
        )
    endif()

    if(NOT GITTOOLS_EXCEPTIONS)
        string(REPLACE "/EHsc" "" CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS}")
        add_compile_options("$<${GITTOOLS_CXX}:/EHs-c->")
        add_compile_definitions(_HAS_EXCEPTIONS=0)
    endif()

    set(GITTOOLS_OPT
        "$<AND:$<COMPILE_LANGUAGE:CXX>,$<OR:$<CONFIG:Release>,$<CONFIG:MinSizeRel>>>")
    set(GITTOOLS_OPT_LINK "$<OR:$<CONFIG:Release>,$<CONFIG:MinSizeRel>>")

    string(REPLACE "/O2" "/Os"
        CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE}")

    add_compile_options(
        "$<${GITTOOLS_OPT}:/Gy>"
        "$<${GITTOOLS_OPT}:/Gw>"
    )
    add_link_options(
        "$<${GITTOOLS_OPT_LINK}:/OPT:REF>"
        "$<${GITTOOLS_OPT_LINK}:/OPT:ICF>"
        "$<${GITTOOLS_OPT_LINK}:/INCREMENTAL:NO>"
    )

    if(GITTOOLS_CLANG_CL)
        add_compile_options("$<${GITTOOLS_OPT}:/clang:-Oz>")
    endif()

    if(GITTOOLS_LTO AND NOT GITTOOLS_CLANG_CL)
        add_compile_options("$<${GITTOOLS_OPT}:/GL>")
        add_link_options("$<${GITTOOLS_OPT_LINK}:/LTCG>")
    elseif(GITTOOLS_LTO AND GITTOOLS_CLANG_CL)
        get_filename_component(GITTOOLS_CLANG_DIR "${CMAKE_CXX_COMPILER}" DIRECTORY)
        find_program(GITTOOLS_LLD_LINK NAMES lld-link HINTS "${GITTOOLS_CLANG_DIR}")
        if(GITTOOLS_LLD_LINK)
            set(CMAKE_LINKER "${GITTOOLS_LLD_LINK}")
            add_compile_options("$<${GITTOOLS_OPT}:-flto=thin>")
            message(STATUS "gittools: ThinLTO via ${GITTOOLS_LLD_LINK}")
        else()
            message(STATUS "gittools: lld-link not found, LTO off")
        endif()
    endif()
endif()

add_compile_definitions(
    UNICODE
    _UNICODE
    NOMINMAX
    WIN32_LEAN_AND_MEAN
)

add_executable(gittools
    src/main.cpp
    src/cli/Alias.cpp
    src/cli/Branches.cpp
    src/cli/Detached.cpp
    src/cli/Dispatch.cpp
    src/cli/Log.cpp
    src/cli/PullLog.cpp
    src/git/Process.cpp
    src/git/CommitStore.cpp
    src/git/Compression.cpp
    src/git/Config.cpp
    src/git/Git.cpp
    src/git/Transcript.cpp
    src/ui/BranchWindow.cpp
    src/ui/DiffWindow.cpp
    src/ui/OptionsDialog.cpp
    src/ui/Encoding.cpp
    src/ui/FindDialog.cpp
    src/ui/Shell.cpp
    src/ui/LogWindow.cpp
    "${GITTOOLS_GENERATED_DIR}/app.rc"
)

target_include_directories(gittools PRIVATE src "${GITTOOLS_GENERATED_DIR}")

target_link_libraries(gittools PRIVATE
    comctl32
    user32
    gdi32
    shell32
    advapi32
    cabinet
    winmm
)

if(MSVC)
    target_link_options(gittools PRIVATE "/MANIFEST:NO")
endif()

set_target_properties(gittools PROPERTIES
    OUTPUT_NAME "gittools"
)
