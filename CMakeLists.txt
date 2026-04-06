cmake_minimum_required(VERSION 3.24)

project(WjSsh VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC OFF)

find_package(Qt6 REQUIRED COMPONENTS Core Gui Widgets Svg)
find_package(PkgConfig REQUIRED)
pkg_check_modules(LIBSSH REQUIRED IMPORTED_TARGET libssh)

qt_standard_project_setup()

qt_add_executable(WjSsh WIN32
    ${CMAKE_CURRENT_SOURCE_DIR}/resources.qrc
    ${CMAKE_CURRENT_SOURCE_DIR}/app.rc
    ${CMAKE_CURRENT_SOURCE_DIR}/src/main.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/SessionProfile.h
    ${CMAKE_CURRENT_SOURCE_DIR}/src/SessionRepository.h
    ${CMAKE_CURRENT_SOURCE_DIR}/src/SessionRepository.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/SshCommon.h
    ${CMAKE_CURRENT_SOURCE_DIR}/src/SshCommon.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/SshShellClient.h
    ${CMAKE_CURRENT_SOURCE_DIR}/src/SshShellClient.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/CommandLineEdit.h
    ${CMAKE_CURRENT_SOURCE_DIR}/src/CommandLineEdit.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/Localization.h
    ${CMAKE_CURRENT_SOURCE_DIR}/src/Localization.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/TerminalView.h
    ${CMAKE_CURRENT_SOURCE_DIR}/src/TerminalView.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/SftpClient.h
    ${CMAKE_CURRENT_SOURCE_DIR}/src/SftpClient.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/RemoteFileEditorDialog.h
    ${CMAKE_CURRENT_SOURCE_DIR}/src/RemoteFileEditorDialog.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/ConnectionPane.h
    ${CMAKE_CURRENT_SOURCE_DIR}/src/ConnectionPane.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/SessionEditorDialog.h
    ${CMAKE_CURRENT_SOURCE_DIR}/src/SessionEditorDialog.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/SystemStatsMonitor.h
    ${CMAKE_CURRENT_SOURCE_DIR}/src/SystemStatsMonitor.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/RemoteServerStatsMonitor.h
    ${CMAKE_CURRENT_SOURCE_DIR}/src/RemoteServerStatsMonitor.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/UiChrome.h
    ${CMAKE_CURRENT_SOURCE_DIR}/src/UiChrome.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/MainWindow.h
    ${CMAKE_CURRENT_SOURCE_DIR}/src/MainWindow.cpp
)

target_include_directories(WjSsh PRIVATE src)

target_link_libraries(WjSsh PRIVATE
    Qt6::Core
    Qt6::Gui
    Qt6::Widgets
    Qt6::Svg
    PkgConfig::LIBSSH
)

if(WIN32)
    target_link_libraries(WjSsh PRIVATE crypt32 dwmapi)
endif()

if(MINGW)
    target_link_options(WjSsh PRIVATE -mwindows)
endif()
