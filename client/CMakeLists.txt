cmake_minimum_required(VERSION 3.20)
project(MuRemaster LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Symlink build/Data/ -> client/Data/ so executables find assets at relative "Data" path
if(NOT EXISTS "${CMAKE_BINARY_DIR}/Data")
    file(CREATE_LINK "${CMAKE_SOURCE_DIR}/Data" "${CMAKE_BINARY_DIR}/Data" SYMBOLIC)
endif()

# Maximum Compiler Optimization for Release Builds
set(CMAKE_CXX_FLAGS_RELEASE "-O3 -ffast-math -DNDEBUG")

# macOS specific configuration
if(APPLE)
    set(CMAKE_OSX_DEPLOYMENT_TARGET "26.0")
    include_directories(/opt/homebrew/include)
    link_directories(/opt/homebrew/lib)
endif()

# Find packages
find_package(glfw3 REQUIRED)
find_package(GLEW REQUIRED)
find_package(OpenGL REQUIRED)
find_package(OpenAL REQUIRED)

# libjpeg-turbo
find_path(JPEG_INCLUDE_DIR NAMES turbojpeg.h PATHS /opt/homebrew/include)
find_library(JPEG_LIBRARY NAMES turbojpeg PATHS /opt/homebrew/lib)

# giflib
find_library(GIF_LIBRARY NAMES gif PATHS /opt/homebrew/lib)

# ImGui setup
set(IMGUI_DIR ${CMAKE_CURRENT_SOURCE_DIR}/external/imgui)
set(IMGUI_SOURCES
    ${IMGUI_DIR}/imgui.cpp
    ${IMGUI_DIR}/imgui_draw.cpp
    ${IMGUI_DIR}/imgui_tables.cpp
    ${IMGUI_DIR}/imgui_widgets.cpp
    ${IMGUI_DIR}/backends/imgui_impl_glfw.cpp
    ${IMGUI_DIR}/backends/imgui_impl_opengl3.cpp
)

# stb headers (stb_image for PNG loading)
set(STB_DIR ${CMAKE_CURRENT_SOURCE_DIR}/external/stb)

# minimp3 (header-only MP3 decoder)
set(MINIMP3_DIR ${CMAKE_CURRENT_SOURCE_DIR}/external/minimp3)

# Source and include directories
include_directories(include)
include_directories(/opt/homebrew/include)
include_directories(${IMGUI_DIR})
include_directories(${IMGUI_DIR}/backends)
include_directories(${STB_DIR})
include_directories(${MINIMP3_DIR})

# Common source files (loaders, parsers, shared infrastructure)
set(COMMON_SRC
    src/TextureLoader.cpp
    src/BMDParser.cpp
    src/BMDUtils.cpp
    src/TerrainParser.cpp
    src/Camera.cpp
    src/Terrain.cpp
    src/Screenshot.cpp
    src/FireEffect.cpp
    src/Sky.cpp
    src/GrassRenderer.cpp
    src/VFXManager.cpp
)

# Main Game Executable
add_executable(MuRemaster src/main.cpp src/InputHandler.cpp src/ObjectRenderer.cpp src/ViewerCommon.cpp src/HeroCharacter.cpp src/ClickEffect.cpp src/NpcManager.cpp src/MonsterManager.cpp src/BoidManager.cpp src/NetworkClient.cpp src/ServerConnection.cpp src/ClientPacketHandler.cpp src/CharacterSelect.cpp src/ItemDatabase.cpp src/ItemModelManager.cpp src/GroundItemRenderer.cpp src/GameUI.cpp src/UITexture.cpp src/UIWidget.cpp src/HUD.cpp src/MockData.cpp src/RayPicker.cpp src/InventoryUI.cpp src/PathFinder.cpp src/SoundManager.cpp ${COMMON_SRC} ${IMGUI_SOURCES})
target_link_libraries(MuRemaster PRIVATE
    glfw
    GLEW::GLEW
    OpenGL::GL
    OpenAL::OpenAL
    ${JPEG_LIBRARY}
    ${GIF_LIBRARY}
)

# Standalone Model Viewer
add_executable(ModelViewer src/model_viewer_main.cpp src/ViewerCommon.cpp ${COMMON_SRC} ${IMGUI_SOURCES})
target_link_libraries(ModelViewer PRIVATE
    glfw
    GLEW::GLEW
    OpenGL::GL
    ${JPEG_LIBRARY}
    ${GIF_LIBRARY}
)

# Standalone Character Viewer
add_executable(CharViewer src/char_viewer_main.cpp src/ViewerCommon.cpp ${COMMON_SRC} ${IMGUI_SOURCES})
target_link_libraries(CharViewer PRIVATE
    glfw
    GLEW::GLEW
    OpenGL::GL
    ${JPEG_LIBRARY}
    ${GIF_LIBRARY}
)


if(APPLE)
    target_link_libraries(MuRemaster PRIVATE "-framework Cocoa" "-framework IOKit" "-framework CoreVideo" "-framework QuartzCore")
    target_link_libraries(ModelViewer PRIVATE "-framework Cocoa" "-framework IOKit" "-framework CoreVideo" "-framework QuartzCore")
    target_link_libraries(CharViewer PRIVATE "-framework Cocoa" "-framework IOKit" "-framework CoreVideo" "-framework QuartzCore")
endif()

message(STATUS "Configured MuRemaster, ModelViewer, and CharViewer")
