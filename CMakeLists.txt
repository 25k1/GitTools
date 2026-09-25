cmake_minimum_required(VERSION 3.25)
project(gittools LANGUAGES C CXX)

if(NOT WIN32 AND NOT (UNIX AND NOT APPLE))
    message(FATAL_ERROR "gittools targets Windows and Linux.")
endif()

if(WIN32)
    enable_language(RC)
endif()

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
configure_file(src/Version.h.in "${GITTOOLS_GENERATED_DIR}/Version.h" @ONLY)
if(WIN32)
    configure_file(resources/app.manifest.in "${GITTOOLS_GENERATED_DIR}/app.manifest" @ONLY)
    configure_file(resources/app.rc.in "${GITTOOLS_GENERATED_DIR}/app.rc" @ONLY)
endif()

set(GITTOOLS_SOUND_DATA "")
set(GITTOOLS_SOUND_CASES "")
foreach(sound IN ITEMS LineInserted:diffLineInserted LineDeleted:diffLineDeleted)
    string(REPLACE ":" ";" sound_parts "${sound}")
    list(GET sound_parts 0 sound_name)
    list(GET sound_parts 1 sound_file)
    set(sound_path "${GITTOOLS_RESOURCE_DIR}/${sound_file}.flac")
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${sound_path}")
    file(READ "${sound_path}" sound_hex HEX)
    string(REGEX REPLACE "([0-9a-f][0-9a-f])" "0x\\1," sound_hex "${sound_hex}")
    string(APPEND GITTOOLS_SOUND_DATA
        "constexpr unsigned char k${sound_name}[] = {${sound_hex}};\n")
    string(APPEND GITTOOLS_SOUND_CASES
        "        case Sound::${sound_name}: return Bytes(k${sound_name});\n")
endforeach()
configure_file(src/audio/Sounds.cpp.in "${GITTOOLS_GENERATED_DIR}/Sounds.cpp" @ONLY)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(GITTOOLS_LTO "Link-time optimisation in optimised builds" ON)
option(GITTOOLS_EXCEPTIONS "Build with C++ exceptions enabled" OFF)
option(GITTOOLS_STATIC_CRT "Statically link the MSVC C runtime" ON)
option(GITTOOLS_HYBRID_CRT "With the static CRT, use the Windows 10+ system UCRT" ON)
option(GITTOOLS_VFE "Drop unused virtual functions with clang LTO" ON)
option(GITTOOLS_LINK_MAP "Write gittools.map next to the executable" OFF)
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
        "$<${GITTOOLS_CXX}:/utf-8>"
        "$<${GITTOOLS_CXX}:/GR->"
    )

    if(NOT GITTOOLS_CLANG_CL)
        add_compile_options(
            "$<${GITTOOLS_CXX}:/permissive->"
            "$<${GITTOOLS_CXX}:/Zc:__cplusplus>"
        )
    endif()

    add_compile_definitions(_USE_STD_VECTOR_ALGORITHMS=0)

    if(NOT GITTOOLS_EXCEPTIONS)
        string(REPLACE "/EHsc" "" CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS}")
        add_compile_options("$<${GITTOOLS_CXX}:/EHs-c->")
        add_compile_definitions(_HAS_EXCEPTIONS=0)
    endif()

    set(GITTOOLS_OPT_LINK "$<OR:$<CONFIG:Release>,$<CONFIG:MinSizeRel>>")
    set(GITTOOLS_OPT "$<AND:$<COMPILE_LANGUAGE:C,CXX>,${GITTOOLS_OPT_LINK}>")
    set(GITTOOLS_OPT_CXX "$<AND:$<COMPILE_LANGUAGE:CXX>,${GITTOOLS_OPT_LINK}>")

    string(REPLACE "/O2" "/Os"
        CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE}")
    string(REPLACE "/O2" "/Os"
        CMAKE_C_FLAGS_RELEASE "${CMAKE_C_FLAGS_RELEASE}")

    add_compile_options(
        "$<${GITTOOLS_OPT}:/Gy>"
        "$<${GITTOOLS_OPT}:/Gw>"
    )
    add_link_options(
        "$<${GITTOOLS_OPT_LINK}:/OPT:REF>"
        "$<${GITTOOLS_OPT_LINK}:/OPT:ICF>"
        "$<${GITTOOLS_OPT_LINK}:/INCREMENTAL:NO>"
    )

    if(GITTOOLS_STATIC_CRT AND GITTOOLS_HYBRID_CRT)
        set(GITTOOLS_UCRT "ucrt$<$<CONFIG:Debug>:d>.lib")
        add_link_options("/NODEFAULTLIB:lib${GITTOOLS_UCRT}"
                         "/DEFAULTLIB:${GITTOOLS_UCRT}")
    endif()

    if(GITTOOLS_CLANG_CL)
        add_compile_options("$<${GITTOOLS_OPT}:/clang:-Oz>")
    endif()

    if(GITTOOLS_LTO AND NOT GITTOOLS_CLANG_CL)
        add_compile_options("$<${GITTOOLS_OPT}:/GL>")
        add_link_options("$<${GITTOOLS_OPT_LINK}:/LTCG>")
    elseif(GITTOOLS_LTO AND GITTOOLS_CLANG_CL)
        get_filename_component(GITTOOLS_CLANG_DIR "${CMAKE_CXX_COMPILER}" DIRECTORY)
        find_program(GITTOOLS_LLD_LINK NAMES lld-link HINTS "${GITTOOLS_CLANG_DIR}")
        find_program(GITTOOLS_LLVM_LIB NAMES llvm-lib HINTS "${GITTOOLS_CLANG_DIR}")
        if(GITTOOLS_LLD_LINK AND GITTOOLS_LLVM_LIB)
            set(CMAKE_LINKER "${GITTOOLS_LLD_LINK}")
            set(CMAKE_AR "${GITTOOLS_LLVM_LIB}")
            add_compile_options("$<${GITTOOLS_OPT}:-flto>")
            if(GITTOOLS_VFE)
                add_compile_options(
                    "$<${GITTOOLS_OPT_CXX}:/clang:-fwhole-program-vtables>"
                    "$<${GITTOOLS_OPT_CXX}:/clang:-fvirtual-function-elimination>"
                )
            endif()
            message(STATUS "gittools: LTO via ${GITTOOLS_LLD_LINK}")
        else()
            message(STATUS "gittools: lld-link or llvm-lib not found, LTO off")
        endif()
    endif()
else()
    set(GITTOOLS_CXX "$<COMPILE_LANGUAGE:CXX>")
    set(GITTOOLS_OPT_LINK "$<OR:$<CONFIG:Release>,$<CONFIG:MinSizeRel>>")
    set(GITTOOLS_OPT "$<AND:$<COMPILE_LANGUAGE:C,CXX>,${GITTOOLS_OPT_LINK}>")
    set(GITTOOLS_OPT_CXX "$<AND:$<COMPILE_LANGUAGE:CXX>,${GITTOOLS_OPT_LINK}>")

    if(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
        set(GITTOOLS_CLANG TRUE)
        set(GITTOOLS_SIZE_FLAG "-Oz")
    else()
        set(GITTOOLS_CLANG FALSE)
        set(GITTOOLS_SIZE_FLAG "-Os")
    endif()
    foreach(lang C CXX)
        string(REPLACE "-O3" "${GITTOOLS_SIZE_FLAG}"
            CMAKE_${lang}_FLAGS_RELEASE "${CMAKE_${lang}_FLAGS_RELEASE}")
    endforeach()

    add_compile_options("$<${GITTOOLS_CXX}:-fno-rtti>")
    if(NOT GITTOOLS_EXCEPTIONS)
        add_compile_options(
            "$<${GITTOOLS_CXX}:-fno-exceptions>"
            "$<${GITTOOLS_OPT}:-fno-unwind-tables>"
            "$<${GITTOOLS_OPT}:-fno-asynchronous-unwind-tables>"
        )
    endif()

    add_compile_options(
        "$<${GITTOOLS_OPT}:-ffunction-sections>"
        "$<${GITTOOLS_OPT}:-fdata-sections>"
    )
    add_link_options(
        "-Wl,--as-needed"
        "$<${GITTOOLS_OPT_LINK}:-Wl,--gc-sections>"
        "$<${GITTOOLS_OPT_LINK}:-s>"
    )

    include(CheckLinkerFlag)
    check_linker_flag(CXX "-Wl,-z,pack-relative-relocs" GITTOOLS_HAS_RELR)
    if(GITTOOLS_HAS_RELR)
        add_link_options("-Wl,-z,pack-relative-relocs")
    endif()

    if(GITTOOLS_LTO AND GITTOOLS_CLANG)
        get_filename_component(GITTOOLS_CLANG_DIR "${CMAKE_CXX_COMPILER}" DIRECTORY)
        string(REGEX MATCH "^[0-9]+" GITTOOLS_CLANG_MAJOR "${CMAKE_CXX_COMPILER_VERSION}")
        find_program(GITTOOLS_LLD
            NAMES ld.lld "ld.lld-${GITTOOLS_CLANG_MAJOR}"
            HINTS "${GITTOOLS_CLANG_DIR}")
        find_program(GITTOOLS_LLVM_AR
            NAMES llvm-ar "llvm-ar-${GITTOOLS_CLANG_MAJOR}"
            HINTS "${GITTOOLS_CLANG_DIR}")
        find_program(GITTOOLS_LLVM_RANLIB
            NAMES llvm-ranlib "llvm-ranlib-${GITTOOLS_CLANG_MAJOR}"
            HINTS "${GITTOOLS_CLANG_DIR}")
        if(GITTOOLS_LLD AND GITTOOLS_LLVM_AR AND GITTOOLS_LLVM_RANLIB)
            set(CMAKE_AR "${GITTOOLS_LLVM_AR}")
            set(CMAKE_RANLIB "${GITTOOLS_LLVM_RANLIB}")
            add_compile_options("$<${GITTOOLS_OPT}:-flto>")
            add_link_options("--ld-path=${GITTOOLS_LLD}" "$<${GITTOOLS_OPT_LINK}:-flto>")
            if(GITTOOLS_VFE)
                add_compile_options(
                    "$<${GITTOOLS_OPT_CXX}:-fwhole-program-vtables>"
                    "$<${GITTOOLS_OPT_CXX}:-fvirtual-function-elimination>"
                )
            endif()
            message(STATUS "gittools: LTO via ${GITTOOLS_LLD}")
        else()
            message(STATUS "gittools: lld or llvm-ar not found, LTO off")
        endif()
    elseif(GITTOOLS_LTO)
        include(CheckIPOSupported)
        check_ipo_supported(RESULT GITTOOLS_IPO LANGUAGES C CXX)
        if(GITTOOLS_IPO)
            set(CMAKE_INTERPROCEDURAL_OPTIMIZATION_RELEASE ON)
            set(CMAKE_INTERPROCEDURAL_OPTIMIZATION_MINSIZEREL ON)
            message(STATUS "gittools: LTO on")
        else()
            message(STATUS "gittools: LTO not supported by this toolchain, off")
        endif()
    endif()
endif()

include(FetchContent)
FetchContent_Declare(
    miniaudio
    URL https://github.com/mackron/miniaudio/archive/refs/tags/0.11.25.tar.gz
    URL_HASH SHA256=b900edcffe979816e2560a0580b9b1216d674b4f17fbadeca8f777a7f8ab0274
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE
    SOURCE_SUBDIR do-not-add-subdirectory
)
FetchContent_MakeAvailable(miniaudio)

set(GITTOOLS_WX_OFF
    wxBUILD_SHARED
    wxBUILD_INSTALL
    wxBUILD_LOCALES
    wxBUILD_SAMPLES
    wxBUILD_TESTS
    wxBUILD_DEMOS
    wxBUILD_BENCHMARKS
    wxUSE_ABOUTDLG
    wxUSE_ACCESSIBILITY
    wxUSE_ACTIVEX
    wxUSE_ACTIVITYINDICATOR
    wxUSE_ADDREMOVECTRL
    wxUSE_AFM_FOR_POSTSCRIPT
    wxUSE_ANIMATIONCTRL
    wxUSE_ARCHIVE_STREAMS
    wxUSE_ARTPROVIDER_STD
    wxUSE_ARTPROVIDER_TANGO
    wxUSE_AUI
    wxUSE_BANNERWINDOW
    wxUSE_BITMAPCOMBOBOX
    wxUSE_BMPBUTTON
    wxUSE_BUSYINFO
    wxUSE_CALENDARCTRL
    wxUSE_CHECKLISTBOX
    wxUSE_CHOICEBOOK
    wxUSE_CHOICEDLG
    wxUSE_CMDLINE_PARSER
    wxUSE_COLLPANE
    wxUSE_COLOURDLG
    wxUSE_COLOURPICKERCTRL
    wxUSE_COMBOBOX
    wxUSE_COMBOCTRL
    wxUSE_COMMANDLINKBUTTON
    wxUSE_CONFIG
    wxUSE_CONSTRAINTS
    wxUSE_CRASHREPORT
    wxUSE_CREDENTIALDLG
    wxUSE_DATAVIEWCTRL
    wxUSE_DATEPICKCTRL
    wxUSE_DATETIME
    wxUSE_DBGHELP
    wxUSE_DC_TRANSFORM_MATRIX
    wxUSE_DEBUGREPORT
    wxUSE_DIALUP_MANAGER
    wxUSE_DIRDLG
    wxUSE_DIRPICKERCTRL
    wxUSE_DOC_VIEW_ARCHITECTURE
    wxUSE_DRAGIMAGE
    wxUSE_DYNAMIC_LOADER
    wxUSE_EDITABLELISTBOX
    wxUSE_EXPAT
    wxUSE_FILE_HISTORY
    wxUSE_FILECTRL
    wxUSE_FILEDLG
    wxUSE_FILEPICKERCTRL
    wxUSE_FILESYSTEM
    wxUSE_FINDREPLDLG
    wxUSE_FONTDLG
    wxUSE_FONTENUM
    wxUSE_FONTMAP
    wxUSE_FONTPICKERCTRL
    wxUSE_FS_ARCHIVE
    wxUSE_FS_INET
    wxUSE_FS_ZIP
    wxUSE_FSVOLUME
    wxUSE_FSWATCHER
    wxUSE_GAUGE
    wxUSE_GIF
    wxUSE_GRID
    wxUSE_HEADERCTRL
    wxUSE_HELP
    wxUSE_HOTKEY
    wxUSE_HTML
    wxUSE_HYPERLINKCTRL
    wxUSE_IFF
    wxUSE_INFOBAR
    wxUSE_INICONF
    wxUSE_INTL
    wxUSE_IPC
    wxUSE_JOYSTICK
    wxUSE_LIBJPEG
    wxUSE_LIBPNG
    wxUSE_LIBTIFF
    wxUSE_LIBWEBP
    wxUSE_LISTBOOK
    wxUSE_LISTBOX
    wxUSE_LOG_DIALOG
    wxUSE_LOGGUI
    wxUSE_LOGWINDOW
    wxUSE_MARKUP
    wxUSE_MDI
    wxUSE_MDI_ARCHITECTURE
    wxUSE_MEDIACTRL
    wxUSE_METAFILE
    wxUSE_MIMETYPE
    wxUSE_MINIFRAME
    wxUSE_MS_HTML_HELP
    wxUSE_NANOSVG
    wxUSE_NATIVE_PROGRESSDLG
    wxUSE_NOTEBOOK
    wxUSE_NOTIFICATION_MESSAGE
    wxUSE_NUMBERDLG
    wxUSE_ODCOMBOBOX
    wxUSE_ON_FATAL_EXCEPTION
    wxUSE_OPENGL
    wxUSE_OWNER_DRAWN
    wxUSE_PCX
    wxUSE_PNM
    wxUSE_POPUPWIN
    wxUSE_POSTSCRIPT
    wxUSE_POSTSCRIPT_ARCHITECTURE_IN_MSW
    wxUSE_PREFERENCES_EDITOR
    wxUSE_PRINTING_ARCHITECTURE
    wxUSE_PRIVATE_FONTS
    wxUSE_PROGRESSDLG
    wxUSE_PROPGRID
    wxUSE_PROTOCOL
    wxUSE_PROTOCOL_FILE
    wxUSE_PROTOCOL_FTP
    wxUSE_PROTOCOL_HTTP
    wxUSE_RADIOBOX
    wxUSE_RADIOBTN
    wxUSE_REARRANGECTRL
    wxUSE_REGEX
    wxUSE_RIBBON
    wxUSE_RICHMSGDLG
    wxUSE_RICHTEXT
    wxUSE_RICHTOOLTIP
    wxUSE_SASH
    wxUSE_SCROLLBAR
    wxUSE_SEARCHCTRL
    wxUSE_SECRETSTORE
    wxUSE_SNGLINST_CHECKER
    wxUSE_SOCKETS
    wxUSE_SOUND
    wxUSE_SPINBTN
    wxUSE_SPINCTRL
    wxUSE_SPLASH
    wxUSE_SPLITTER
    wxUSE_STACKWALKER
    wxUSE_STARTUP_TIPS
    wxUSE_STATBMP
    wxUSE_STATBOX
    wxUSE_STATLINE
    wxUSE_STC
    wxUSE_STD_IOSTREAM
    wxUSE_SVG
    wxUSE_TARSTREAM
    wxUSE_TASKBARBUTTON
    wxUSE_TASKBARICON
    wxUSE_TASKBARICON_BALLOONS
    wxUSE_TEXTDLG
    wxUSE_TGA
    wxUSE_TIMEPICKCTRL
    wxUSE_TIPWINDOW
    wxUSE_TOGGLEBTN
    wxUSE_TOOLBAR
    wxUSE_TOOLBAR_NATIVE
    wxUSE_TOOLBOOK
    wxUSE_TREEBOOK
    wxUSE_TREECTRL
    wxUSE_TREELISTCTRL
    wxUSE_UIACTIONSIMULATOR
    wxUSE_URL
    wxUSE_VALIDATORS
    wxUSE_WEBREQUEST
    wxUSE_WEBVIEW
    wxUSE_WIZARDDLG
    wxUSE_WXHTML_HELP
    wxUSE_XLOCALE
    wxUSE_XML
    wxUSE_XPM
    wxUSE_XRC
    wxUSE_ZIPSTREAM
    wxUSE_ZLIB
)
if(WIN32)
    list(APPEND GITTOOLS_WX_OFF
        wxUSE_GRAPHICS_CONTEXT
        wxUSE_TEXTBUFFER
        wxUSE_TEXTFILE
    )
else()
    list(APPEND GITTOOLS_WX_OFF
        wxUSE_GTKPRINT
        wxUSE_LIBMSPACK
        wxUSE_LIBNOTIFY
        wxUSE_LIBSDL
        wxUSE_SPELLCHECK
        wxUSE_XTEST
    )
endif()
foreach(wx_setting IN LISTS GITTOOLS_WX_OFF)
    set(${wx_setting} OFF CACHE BOOL gittools FORCE)
endforeach()
set(wxBUILD_MONOLITHIC ON CACHE BOOL gittools FORCE)
set(wxBUILD_COMPATIBILITY NONE CACHE STRING gittools FORCE)
if(MSVC)
    set(wxBUILD_USE_STATIC_RUNTIME ${GITTOOLS_STATIC_CRT} CACHE BOOL gittools FORCE)
endif()
set(wxBUILD_DEBUG_LEVEL 0 CACHE STRING gittools FORCE)
set(wxUSE_EXCEPTIONS ${GITTOOLS_EXCEPTIONS} CACHE BOOL gittools FORCE)
set(wxUSE_NO_RTTI ON CACHE BOOL gittools FORCE)

FetchContent_Declare(
    wxWidgets
    URL https://github.com/wxWidgets/wxWidgets/releases/download/v3.3.3/wxWidgets-3.3.3.tar.bz2
    URL_HASH SHA256=81b09d6dd9f1ed9301f8c55a968a488d0491f264dc2bab19a7e407ac67009482
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE
    SYSTEM
)
set(CMAKE_CXX_STANDARD 14)
FetchContent_MakeAvailable(wxWidgets)
set(CMAKE_CXX_STANDARD 20)

add_executable(gittools
    src/main.cpp
    src/audio/Audio.cpp
    src/cli/Alias.cpp
    src/cli/Branches.cpp
    src/cli/Detached.cpp
    src/cli/Dispatch.cpp
    src/cli/Log.cpp
    src/cli/PullLog.cpp
    src/git/CommitStore.cpp
    src/git/Config.cpp
    src/git/Git.cpp
    src/git/Transcript.cpp
    src/ui/App.cpp
    src/ui/BranchWindow.cpp
    src/ui/Columns.cpp
    src/ui/ColumnsDialog.cpp
    src/ui/DiffWindow.cpp
    src/ui/FindDialog.cpp
    src/ui/LogWindow.cpp
    src/ui/OptionsDialog.cpp
    src/ui/ToolFrame.cpp
    src/ui/Widgets.cpp
    "${GITTOOLS_GENERATED_DIR}/Sounds.cpp"
)

if(WIN32)
    set(GITTOOLS_PLATFORM_DIR src/platform/win)
else()
    set(GITTOOLS_PLATFORM_DIR src/platform/posix)
endif()
target_sources(gittools PRIVATE
    ${GITTOOLS_PLATFORM_DIR}/Compression.cpp
    ${GITTOOLS_PLATFORM_DIR}/Encoding.cpp
    ${GITTOOLS_PLATFORM_DIR}/Process.cpp
    ${GITTOOLS_PLATFORM_DIR}/Shell.cpp
    ${GITTOOLS_PLATFORM_DIR}/System.cpp
)
if(WIN32)
    target_sources(gittools PRIVATE "${GITTOOLS_GENERATED_DIR}/app.rc")
endif()
if(MSVC AND GITTOOLS_STATIC_CRT AND NOT GITTOOLS_EXCEPTIONS)
    target_sources(gittools PRIVATE src/platform/win/StdHooks.cpp)
endif()

target_include_directories(gittools PRIVATE src "${GITTOOLS_GENERATED_DIR}")
target_include_directories(gittools SYSTEM PRIVATE "${miniaudio_SOURCE_DIR}")

target_compile_definitions(gittools PRIVATE
    wxDEBUG_LEVEL=${wxBUILD_DEBUG_LEVEL}
)
target_link_libraries(gittools PRIVATE wxWidgets::wxWidgets)

if(WIN32)
    target_compile_definitions(gittools PRIVATE
        UNICODE
        _UNICODE
        NOMINMAX
        WIN32_LEAN_AND_MEAN
    )
    target_link_libraries(gittools PRIVATE
        user32
        shell32
        advapi32
        cabinet
    )
else()
    find_package(ZLIB REQUIRED)
    find_package(Threads REQUIRED)
    target_link_libraries(gittools PRIVATE
        ZLIB::ZLIB
        Threads::Threads
        ${CMAKE_DL_LIBS}
        m
    )
endif()

if(MSVC)
    target_compile_options(gittools PRIVATE "$<${GITTOOLS_CXX}:/W4>")
    target_link_options(gittools PRIVATE "/MANIFEST:NO")
    if(GITTOOLS_LINK_MAP)
        target_link_options(gittools PRIVATE "/MAP")
    endif()
else()
    target_compile_options(gittools PRIVATE
        -Wall
        -Wextra
        -fvisibility=hidden
        -fvisibility-inlines-hidden
    )
    if(GITTOOLS_LINK_MAP)
        target_link_options(gittools PRIVATE
            "-Wl,-Map=$<TARGET_FILE_DIR:gittools>/gittools.map")
    endif()
endif()

set_target_properties(gittools PROPERTIES
    OUTPUT_NAME "gittools"
)
