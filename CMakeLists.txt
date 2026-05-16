cmake_minimum_required(VERSION 3.25)
project(gittools LANGUAGES CXX)

if(NOT WIN32)
    message(FATAL_ERROR "gittools targets Windows only.")
endif()

enable_language(RC)

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
    resources/app.rc
)

target_include_directories(gittools PRIVATE src)

target_link_libraries(gittools PRIVATE
    comctl32
    user32
    gdi32
    shell32
    advapi32
    winmm
)

if(MSVC)
    target_link_options(gittools PRIVATE "/MANIFEST:NO")
endif()

set_target_properties(gittools PROPERTIES
    OUTPUT_NAME "gittools"
)
