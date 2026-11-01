cmake_minimum_required(VERSION 3.24)

# Only has an effect on macOS, but must be set before project().
set(CMAKE_OSX_DEPLOYMENT_TARGET "11.0" CACHE STRING "Oldest macOS version to support")

project(Fetcher VERSION 1.0.0 LANGUAGES C CXX)

if(WIN32)
    enable_language(RC)
elseif(APPLE)
    enable_language(OBJC OBJCXX)
endif()

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(FETCHER_SHOW_ASSERTS "Show wxWidgets assertion failures (for development)" OFF)

include(cmake/Dependencies.cmake)

# ---------------------------------------------------------------------------
# Core library: no GUI, so it can be tested without a display.
# ---------------------------------------------------------------------------
add_library(fetcher_core STATIC
    src/core/http_client.cpp
    src/core/serialize.cpp
    src/core/store.cpp
    src/core/text.cpp
    src/core/url.cpp
    src/core/workspace.cpp
)
target_include_directories(fetcher_core PUBLIC src)
target_compile_definitions(fetcher_core PUBLIC FETCHER_VERSION="${PROJECT_VERSION}")
target_link_libraries(fetcher_core PRIVATE nlohmann_json)

if(WIN32)
    target_sources(fetcher_core PRIVATE src/core/http_winhttp.cpp)
    target_link_libraries(fetcher_core PUBLIC winhttp)
else()
    find_package(CURL REQUIRED)
    target_sources(fetcher_core PRIVATE src/core/http_curl.cpp)
    target_link_libraries(fetcher_core PUBLIC CURL::libcurl)
endif()

if(MINGW)
    # Link the C++ runtime statically so the .exe stands alone.
    target_link_options(fetcher_core PUBLIC -static)
endif()

# ---------------------------------------------------------------------------
# Desktop app
# ---------------------------------------------------------------------------
add_executable(fetcher WIN32 MACOSX_BUNDLE
    src/app/app.cpp
    src/app/code_editor.cpp
    src/app/dialogs.cpp
    src/app/document_tabs.cpp
    src/app/fonts.cpp
    src/app/key_value_editor.cpp
    src/app/main_frame.cpp
    src/app/request_panel.cpp
    src/app/response_view.cpp
    src/app/sidebar.cpp
    src/app/theme.cpp
    src/app/widgets.cpp
)
target_link_libraries(fetcher PRIVATE fetcher_core wx::core wx::base wx::stc)
if(FETCHER_SHOW_ASSERTS)
    target_compile_definitions(fetcher PRIVATE FETCHER_SHOW_ASSERTS)
endif()

if(WIN32)
    target_sources(fetcher PRIVATE packaging/windows/fetcher.rc)
    target_link_libraries(fetcher PRIVATE dwmapi)
elseif(APPLE)
    set(FETCHER_ICNS packaging/macos/Fetcher.icns)
    target_sources(fetcher PRIVATE ${FETCHER_ICNS})
    set_source_files_properties(${FETCHER_ICNS} PROPERTIES MACOSX_PACKAGE_LOCATION Resources)
    file(GLOB FETCHER_FONTS CONFIGURE_DEPENDS resources/fonts/*)
    target_sources(fetcher PRIVATE ${FETCHER_FONTS})
    set_source_files_properties(${FETCHER_FONTS} PROPERTIES MACOSX_PACKAGE_LOCATION Resources/Fonts)
    set_target_properties(fetcher PROPERTIES
        OUTPUT_NAME Fetcher
        MACOSX_BUNDLE_INFO_PLIST ${CMAKE_CURRENT_SOURCE_DIR}/packaging/macos/Info.plist.in
        MACOSX_BUNDLE_BUNDLE_NAME Fetcher
        MACOSX_BUNDLE_GUI_IDENTIFIER io.github.jbktn.fetcher
        MACOSX_BUNDLE_ICON_FILE Fetcher.icns
        MACOSX_BUNDLE_BUNDLE_VERSION ${PROJECT_VERSION}
        MACOSX_BUNDLE_SHORT_VERSION_STRING ${PROJECT_VERSION}
    )
else()
    target_include_directories(fetcher PRIVATE packaging/linux)
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(GTK3 REQUIRED IMPORTED_TARGET gtk+-3.0)
    target_link_libraries(fetcher PRIVATE PkgConfig::GTK3)
endif()

if(NOT APPLE)
    # So the app finds its fonts when it runs straight from the build folder.
    add_custom_command(TARGET fetcher POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_directory
                ${CMAKE_CURRENT_SOURCE_DIR}/resources/fonts $<TARGET_FILE_DIR:fetcher>/fonts)
endif()

# ---------------------------------------------------------------------------
# Tests
# ---------------------------------------------------------------------------
include(CTest)
if(BUILD_TESTING)
    add_executable(core_tests tests/core_tests.cpp)
    target_link_libraries(core_tests PRIVATE fetcher_core nlohmann_json)
    if(WIN32)
        target_link_libraries(core_tests PRIVATE ws2_32)
    else()
        find_package(Threads REQUIRED)
        target_link_libraries(core_tests PRIVATE Threads::Threads)
    endif()
    add_test(NAME core_tests COMMAND core_tests)
endif()

# ---------------------------------------------------------------------------
# Installing and packaging
# ---------------------------------------------------------------------------
if(APPLE)
    install(TARGETS fetcher BUNDLE DESTINATION .)
    set(CPACK_GENERATOR DragNDrop)
elseif(WIN32)
    install(TARGETS fetcher RUNTIME DESTINATION .)
    install(FILES LICENSE DESTINATION .)
    install(DIRECTORY resources/fonts/ DESTINATION fonts)
    set(CPACK_GENERATOR ZIP)
else()
    include(GNUInstallDirs)
    install(TARGETS fetcher RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
    install(FILES packaging/linux/fetcher.desktop
            DESTINATION ${CMAKE_INSTALL_DATADIR}/applications)
    install(FILES packaging/linux/fetcher.png
            DESTINATION ${CMAKE_INSTALL_DATADIR}/icons/hicolor/256x256/apps)
    install(FILES LICENSE DESTINATION ${CMAKE_INSTALL_DOCDIR})
    install(DIRECTORY resources/fonts/ DESTINATION ${CMAKE_INSTALL_DATADIR}/fetcher/fonts)

    # Always a tarball, plus the distribution packages this machine can make.
    set(CPACK_GENERATOR TGZ)
    find_program(FETCHER_DPKG_SHLIBDEPS dpkg-shlibdeps)
    if(FETCHER_DPKG_SHLIBDEPS)
        list(APPEND CPACK_GENERATOR DEB)
        set(CPACK_DEBIAN_PACKAGE_SHLIBDEPS ON)
        set(CPACK_DEBIAN_PACKAGE_MAINTAINER "Fetcher contributors")
        set(CPACK_DEBIAN_PACKAGE_SECTION devel)
    endif()
    find_program(FETCHER_RPMBUILD rpmbuild)
    if(FETCHER_RPMBUILD)
        list(APPEND CPACK_GENERATOR RPM)
        set(CPACK_RPM_PACKAGE_LICENSE MIT)
        set(CPACK_RPM_PACKAGE_AUTOREQ ON)
    endif()
endif()

set(CPACK_PACKAGE_NAME Fetcher)
set(CPACK_PACKAGE_VENDOR Fetcher)
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "Test API calls, modify them and save them")
set(CPACK_RESOURCE_FILE_LICENSE ${CMAKE_CURRENT_SOURCE_DIR}/LICENSE)
set(CPACK_PACKAGE_FILE_NAME "Fetcher-${PROJECT_VERSION}-${CMAKE_SYSTEM_NAME}")
include(CPack)
