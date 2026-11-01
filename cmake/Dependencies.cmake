# Third-party code, downloaded at configure time and pinned by checksum.
include(FetchContent)

# nlohmann/json is a single header.
set(FETCHER_JSON_HEADER ${CMAKE_BINARY_DIR}/_deps/json/include/nlohmann/json.hpp)
file(DOWNLOAD
    https://github.com/nlohmann/json/releases/download/v3.12.0/json.hpp
    ${FETCHER_JSON_HEADER}
    EXPECTED_HASH SHA256=aaf127c04cb31c406e5b04a63f1ae89369fccde6d8fa7cdda1ed4f32dfc5de63
    TLS_VERIFY ON
)
add_library(nlohmann_json INTERFACE)
target_include_directories(nlohmann_json SYSTEM INTERFACE ${CMAKE_BINARY_DIR}/_deps/json/include)

# wxWidgets, linked statically so the app ships as a single executable or bundle.
set(wxBUILD_SHARED OFF CACHE BOOL "" FORCE)
set(wxBUILD_INSTALL OFF CACHE BOOL "" FORCE)
set(wxBUILD_SAMPLES OFF CACHE STRING "" FORCE)
set(wxBUILD_TESTS OFF CACHE STRING "" FORCE)
set(wxBUILD_DEMOS OFF CACHE BOOL "" FORCE)
set(wxBUILD_LOCALES OFF CACHE STRING "" FORCE)

# Bundled copies of image libraries avoid extra runtime dependencies on Linux.
set(wxUSE_LIBPNG builtin CACHE STRING "" FORCE)
set(wxUSE_LIBJPEG builtin CACHE STRING "" FORCE)
set(wxUSE_ZLIB builtin CACHE STRING "" FORCE)
set(wxUSE_EXPAT builtin CACHE STRING "" FORCE)
set(wxUSE_REGEX builtin CACHE STRING "" FORCE)
set(wxUSE_LIBTIFF OFF CACHE STRING "" FORCE)
set(wxUSE_LIBWEBP OFF CACHE STRING "" FORCE)

# The bundled fonts need this. wxWidgets only turns it on by itself off Linux.
set(wxUSE_PRIVATE_FONTS ON CACHE BOOL "" FORCE)

# Parts of wxWidgets this app doesn't use. Leaving them out shortens the build
# and shrinks the binary.
foreach(unused
    wxUSE_WEBREQUEST wxUSE_WEBVIEW wxUSE_MEDIACTRL wxUSE_OPENGL wxUSE_GLCANVAS
    wxUSE_RIBBON wxUSE_RICHTEXT wxUSE_PROPGRID wxUSE_XRC wxUSE_XML
    wxUSE_HTML wxUSE_HELP wxUSE_WXHTML_HELP wxUSE_MS_HTML_HELP
    wxUSE_SOUND wxUSE_SECRETSTORE wxUSE_LIBNOTIFY wxUSE_APPINDICATOR wxUSE_LIBSDL
    wxUSE_LIBGNOMEVFS wxUSE_LIBMSPACK wxUSE_GTKPRINT wxUSE_DEBUGREPORT)
    set(${unused} OFF CACHE BOOL "" FORCE)
endforeach()

# A precompiled header can't be shared between architectures, so a universal
# macOS build goes without one.
list(LENGTH CMAKE_OSX_ARCHITECTURES FETCHER_ARCH_COUNT)
if(FETCHER_ARCH_COUNT GREATER 1)
    set(wxBUILD_PRECOMP OFF CACHE STRING "" FORCE)
endif()

FetchContent_Declare(wxWidgets
    URL https://github.com/wxWidgets/wxWidgets/releases/download/v3.3.4/wxWidgets-3.3.4.tar.bz2
    URL_HASH SHA256=815c16cafa4fcbfcf25c5c0a3418309c62257b8a3b6f91d85a1367a134f98c98
    DOWNLOAD_EXTRACT_TIMESTAMP FALSE
)
FetchContent_MakeAvailable(wxWidgets)
