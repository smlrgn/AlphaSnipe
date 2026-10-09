cmake_minimum_required(VERSION 3.16)
project(alphasnipe VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(SRC
    src/main.cpp
    src/app.cpp
    src/image.cpp
    src/font.cpp
    src/icons.cpp
    src/stb_impl.cpp
)

if(WIN32)
    list(APPEND SRC src/platform_win32.cpp)
elseif(APPLE)
    list(APPEND SRC src/platform_macos.mm)
    set_source_files_properties(src/platform_macos.mm PROPERTIES COMPILE_FLAGS "-fobjc-arc")
else()
    list(APPEND SRC src/platform_x11.cpp)
endif()

add_executable(alphasnipe ${SRC})
target_include_directories(alphasnipe PRIVATE src third_party)

if(MSVC)
    target_compile_options(alphasnipe PRIVATE /W3 /utf-8 /DNOMINMAX /DUNICODE /D_UNICODE)
else()
    target_compile_options(alphasnipe PRIVATE -Wall -Wextra -Wno-unused-parameter)
endif()

if(WIN32)
    target_compile_definitions(alphasnipe PRIVATE UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX)
    target_link_libraries(alphasnipe PRIVATE gdi32 user32 shell32 comdlg32 ole32 uuid comctl32)
    set_target_properties(alphasnipe PROPERTIES WIN32_EXECUTABLE TRUE OUTPUT_NAME AlphaSnipe)
    set(SNIP_ICON "${CMAKE_SOURCE_DIR}/assets/alphasnipe.ico")
    set(SNIP_MANIFEST "${CMAKE_SOURCE_DIR}/src/app.manifest")
    configure_file(${CMAKE_SOURCE_DIR}/src/app.rc.in ${CMAKE_BINARY_DIR}/app.rc @ONLY)
    if(MINGW)
        target_link_options(alphasnipe PRIVATE -static -static-libgcc -static-libstdc++ -mwindows)
        enable_language(RC)
        target_sources(alphasnipe PRIVATE ${CMAKE_BINARY_DIR}/app.rc)
    elseif(MSVC)
        target_sources(alphasnipe PRIVATE ${CMAKE_BINARY_DIR}/app.rc)
    endif()
elseif(APPLE)
    set_target_properties(alphasnipe PROPERTIES
        MACOSX_BUNDLE TRUE
        MACOSX_BUNDLE_BUNDLE_NAME "AlphaSnipe"
        MACOSX_BUNDLE_GUI_IDENTIFIER "app.alphasnipe"
        MACOSX_BUNDLE_BUNDLE_VERSION ${PROJECT_VERSION}
        MACOSX_BUNDLE_SHORT_VERSION_STRING ${PROJECT_VERSION}
        MACOSX_BUNDLE_ICON_FILE alphasnipe.icns
        OUTPUT_NAME "AlphaSnipe")
    target_link_libraries(alphasnipe PRIVATE
        "-framework Cocoa"
        "-framework ApplicationServices"
        "-framework CoreGraphics"
        "-framework ImageIO")
else()
    find_package(X11 REQUIRED)
    target_link_libraries(alphasnipe PRIVATE X11::X11)
    if(TARGET X11::Xext)
        target_link_libraries(alphasnipe PRIVATE X11::Xext)
    else()
        target_link_libraries(alphasnipe PRIVATE Xext)
    endif()
    target_link_libraries(alphasnipe PRIVATE pthread)
    set_target_properties(alphasnipe PROPERTIES OUTPUT_NAME alphasnipe)
endif()

add_custom_command(TARGET alphasnipe POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${CMAKE_SOURCE_DIR}/assets/DejaVuSans.ttf"
            "$<TARGET_FILE_DIR:alphasnipe>/DejaVuSans.ttf"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${CMAKE_SOURCE_DIR}/assets/alphasnipe.png"
            "$<TARGET_FILE_DIR:alphasnipe>/alphasnipe.png")

install(TARGETS alphasnipe RUNTIME DESTINATION bin BUNDLE DESTINATION .)
install(FILES assets/DejaVuSans.ttf DESTINATION share/alphasnipe)
if(UNIX AND NOT APPLE)
    install(FILES assets/alphasnipe.desktop DESTINATION share/applications)
    install(FILES assets/alphasnipe.png DESTINATION share/icons/hicolor/256x256/apps)
endif()
