cmake_minimum_required(VERSION 3.16)
project(AetherPlayer LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

find_package(Qt6 REQUIRED COMPONENTS Core Gui Widgets Multimedia Network Concurrent OpenGL OpenGLWidgets)

set(SOURCES
        main.cpp
        AudioTrack.h
        Widgets.h
        Widgets.cpp
        Workers.h
        Workers.cpp
        LiquidGlassPlayer.h
        LiquidGlassPlayer.cpp
        miniaudio.h
        SmtcManager.h
        SmtcManager.cpp
        DlnaCastService.h
        DlnaCastService.cpp
)
add_executable(AetherPlayer WIN32 ${SOURCES})
target_link_libraries(AetherPlayer PRIVATE
        Qt6::Core
        Qt6::Gui
        Qt6::Widgets
        Qt6::Multimedia
        Qt6::Network
        Qt6::Concurrent
        Qt6::OpenGL
        Qt6::OpenGLWidgets
)

if (WIN32)
    target_link_libraries(AetherPlayer PRIVATE dwmapi user32 shell32 ole32 opengl32 psapi windowsapp runtimeobject)
    if (MINGW)
        target_link_options(AetherPlayer PRIVATE -mwindows -static-libgcc -static-libstdc++ -Wl,-Bstatic -lwinpthread -Wl,-Bdynamic)
    endif()
    get_target_property(_qmake_executable Qt6::qmake IMPORTED_LOCATION)
    get_filename_component(_qt_bin_dir "${_qmake_executable}" DIRECTORY)
    find_program(WINDEPLOYQT_EXECUTABLE windeployqt HINTS "${_qt_bin_dir}")

    if (WINDEPLOYQT_EXECUTABLE)
        add_custom_command(TARGET AetherPlayer POST_BUILD
                COMMAND "${WINDEPLOYQT_EXECUTABLE}"
                --compiler-runtime
                "$<TARGET_FILE:AetherPlayer>"
                COMMENT "Автоматическая упаковка всех зависимостей (windeployqt)..."
        )
    endif()

endif()

