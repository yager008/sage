cmake_minimum_required(VERSION 3.15)
project(VEngineWebGL LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(IMGUI_DIR "${CMAKE_CURRENT_LIST_DIR}/../imgui")
set(SOURCES
    src/main.cpp
    ${IMGUI_DIR}/imgui.cpp
    ${IMGUI_DIR}/imgui_draw.cpp
    ${IMGUI_DIR}/imgui_tables.cpp
    ${IMGUI_DIR}/imgui_widgets.cpp
    ${IMGUI_DIR}/misc/cpp/imgui_stdlib.cpp
    ${IMGUI_DIR}/backends/imgui_impl_sdl2.cpp
    ${IMGUI_DIR}/backends/imgui_impl_opengl3.cpp
)

add_executable(vengine_webgl ${SOURCES})
target_include_directories(vengine_webgl PRIVATE
    ${IMGUI_DIR}
    ${IMGUI_DIR}/backends
    ${IMGUI_DIR}/misc/cpp
)

target_compile_definitions(vengine_webgl PRIVATE IMGUI_IMPL_OPENGL_ES3)

if (EMSCRIPTEN)
    set(CMAKE_EXECUTABLE_SUFFIX ".html")
    target_compile_options(vengine_webgl PRIVATE
        "-sUSE_SDL=2"
        "-sFULL_ES3=1"
        "-sALLOW_MEMORY_GROWTH=1"
        "-sASSERTIONS=1"
        "-sMIN_WEBGL_VERSION=2"
        "-sMAX_WEBGL_VERSION=2"
    )
    target_link_options(vengine_webgl PRIVATE
        "-sUSE_SDL=2"
        "-sFULL_ES3=1"
        "-sWASM=1"
        "-sALLOW_MEMORY_GROWTH=1"
        "-sASSERTIONS=1"
        "-sDISABLE_EXCEPTION_CATCHING=0"
        "-sMIN_WEBGL_VERSION=2"
        "-sMAX_WEBGL_VERSION=2"
        "--preload-file"
        "${CMAKE_CURRENT_SOURCE_DIR}/assets@/assets"
    )
else()
    find_package(SDL2 REQUIRED)
    target_include_directories(vengine_webgl PRIVATE ${SDL2_INCLUDE_DIRS})
    target_link_libraries(vengine_webgl PRIVATE ${SDL2_LIBRARIES})
    if (APPLE)
        target_link_libraries(vengine_webgl PRIVATE "-framework OpenGL")
    elseif (WIN32)
        target_link_libraries(vengine_webgl PRIVATE opengl32)
    else()
        target_link_libraries(vengine_webgl PRIVATE GL)
    endif()
endif()
