cmake_minimum_required(VERSION 3.20)
project(AnCoreBrowser VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(SOURCES
    src/main.cpp
    src/mainwindow.cpp
    src/browsercore.cpp
    src/saveas.cpp
    src/contextmenu.cpp
)

set(HEADERS
    src/mainwindow.h
    src/browsercore.h
    src/saveas.h
    src/contextmenu.h
)

set(APP_RESOURCES src/AnCoreBrowser.rc)

add_executable(${PROJECT_NAME} WIN32 ${SOURCES} ${HEADERS} ${APP_RESOURCES})

target_include_directories(${PROJECT_NAME} PRIVATE src)

target_link_libraries(${PROJECT_NAME} PRIVATE
    comctl32
    comdlg32
    dwmapi
    uxtheme
    ole32
    oleaut32
    shell32
    shlwapi
    urlmon
    gdiplus
    uuid
    ${CMAKE_SOURCE_DIR}/WebView2LoaderStatic.lib
)

target_compile_definitions(${PROJECT_NAME} PRIVATE
    ANCORE_VERSION="${PROJECT_VERSION}"
    ANCORE_RESOURCES_DIR="${CMAKE_SOURCE_DIR}/resources"
    _UNICODE
    UNICODE
    WIN32_LEAN_AND_MEAN
    NOMINMAX
)

# UTF-8 исходники → корректные L"..." и кириллица в Win32-меню
if(MSVC)
    target_compile_options(${PROJECT_NAME} PRIVATE /utf-8)
endif()

# Copy resources to build output
add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${CMAKE_SOURCE_DIR}/resources"
        "$<TARGET_FILE_DIR:${PROJECT_NAME}>/resources"
    COMMENT "Copying resources to build output"
)

# Deploy to deploy/AnCoreBrowser/Application
add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${CMAKE_SOURCE_DIR}/resources"
        "${CMAKE_SOURCE_DIR}/deploy/AnCoreBrowser/Application/resources"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "$<TARGET_FILE:${PROJECT_NAME}>"
        "${CMAKE_SOURCE_DIR}/deploy/AnCoreBrowser/Application/$<TARGET_FILE_NAME:${PROJECT_NAME}>"
    COMMENT "Deploying to deploy/AnCoreBrowser/Application"
)
