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
find_package(OpenAL REQUIRED)

# BGFX setup
set(BGFX_BUILD_TOOLS ON CACHE BOOL "" FORCE)
set(BGFX_BUILD_TOOLS_SHADER ON CACHE BOOL "" FORCE)
set(BGFX_BUILD_TOOLS_GEOMETRY OFF CACHE BOOL "" FORCE)
set(BGFX_BUILD_TOOLS_TEXTURE OFF CACHE BOOL "" FORCE)
set(BGFX_BUILD_TOOLS_BIN2C OFF CACHE BOOL "" FORCE)
set(BGFX_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(BGFX_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(BGFX_INSTALL OFF CACHE BOOL "" FORCE)
set(BGFX_CUSTOM_TARGETS OFF CACHE BOOL "" FORCE)
add_subdirectory(external/bgfx.cmake)

# libjpeg-turbo
find_path(JPEG_INCLUDE_DIR NAMES turbojpeg.h PATHS /opt/homebrew/include)
find_library(JPEG_LIBRARY NAMES turbojpeg PATHS /opt/homebrew/lib)

# giflib
find_library(GIF_LIBRARY NAMES gif PATHS /opt/homebrew/lib)

# ImGui setup (BGFX backend)
set(IMGUI_DIR ${CMAKE_CURRENT_SOURCE_DIR}/external/imgui)
set(IMGUI_SOURCES
    ${IMGUI_DIR}/imgui.cpp
    ${IMGUI_DIR}/imgui_draw.cpp
    ${IMGUI_DIR}/imgui_tables.cpp
    ${IMGUI_DIR}/imgui_widgets.cpp
    ${IMGUI_DIR}/backends/imgui_impl_glfw.cpp
    src/imgui_impl_bgfx.cpp
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
    src/TerrainUtils.cpp
    src/VFXManager.cpp
    src/VFXManagerElemental.cpp
    src/VFXManagerSpirit.cpp
    src/VFXManagerMelee.cpp
    src/ChromeGlow.cpp
)

# Rendering libraries
set(RENDER_LIBS bgfx bimg bx)

# Main Game Executable
add_executable(MuRemaster src/main.cpp src/InputHandler.cpp src/ObjectRenderer.cpp src/ViewerCommon.cpp src/HeroCharacter.cpp src/HeroCharacterMount.cpp src/HeroCharacterPet.cpp src/ClickEffect.cpp src/NpcManager.cpp src/MonsterManager.cpp src/MonsterManagerRender.cpp src/MonsterManagerEffects.cpp src/BoidManager.cpp src/NetworkClient.cpp src/ServerConnection.cpp src/ClientPacketHandler.cpp src/CharacterSelect.cpp src/ItemDatabase.cpp src/ItemModelManager.cpp src/GroundItemRenderer.cpp src/GameUI.cpp src/UITexture.cpp src/UIWidget.cpp src/HUD.cpp src/MockData.cpp src/RayPicker.cpp src/InventoryUI.cpp src/InventoryUITooltip.cpp src/InventoryUISkills.cpp src/PathFinder.cpp src/SoundManager.cpp src/SystemMessageLog.cpp ${COMMON_SRC} ${IMGUI_SOURCES})
target_link_libraries(MuRemaster PRIVATE
    glfw
    ${RENDER_LIBS}
    OpenAL::OpenAL
    ${JPEG_LIBRARY}
    ${GIF_LIBRARY}
)

# Standalone Model Viewer
add_executable(ModelViewer src/model_viewer_main.cpp src/ViewerCommon.cpp ${COMMON_SRC} ${IMGUI_SOURCES})
target_link_libraries(ModelViewer PRIVATE
    glfw
    ${RENDER_LIBS}
    ${JPEG_LIBRARY}
    ${GIF_LIBRARY}
)

# Standalone Character Viewer
add_executable(CharViewer src/char_viewer_main.cpp src/ViewerCommon.cpp ${COMMON_SRC} ${IMGUI_SOURCES})
target_link_libraries(CharViewer PRIVATE
    glfw
    ${RENDER_LIBS}
    ${JPEG_LIBRARY}
    ${GIF_LIBRARY}
)

if(APPLE)
    set(APPLE_FRAMEWORKS "-framework Cocoa" "-framework IOKit" "-framework CoreVideo" "-framework QuartzCore" "-framework Metal" "-framework MetalKit" "-framework ImageIO" "-framework CoreGraphics")
    target_link_libraries(MuRemaster PRIVATE ${APPLE_FRAMEWORKS})
    target_link_libraries(ModelViewer PRIVATE ${APPLE_FRAMEWORKS})
    target_link_libraries(CharViewer PRIVATE ${APPLE_FRAMEWORKS})
endif()

# BGFX initialization test executable
add_executable(BgfxInitTest src/bgfx_init_test.cpp src/TextureLoader.cpp src/UITexture.cpp src/BMDParser.cpp src/BMDUtils.cpp src/ViewerCommon.cpp src/Sky.cpp src/Terrain.cpp src/TerrainParser.cpp src/TerrainUtils.cpp src/GrassRenderer.cpp src/ObjectRenderer.cpp src/SoundManager.cpp src/FireEffect.cpp src/BoidManager.cpp src/ClickEffect.cpp src/NpcManager.cpp src/ChromeGlow.cpp src/HeroCharacter.cpp src/HeroCharacterMount.cpp src/HeroCharacterPet.cpp src/ItemModelManager.cpp src/ItemDatabase.cpp src/VFXManager.cpp src/VFXManagerMelee.cpp src/VFXManagerElemental.cpp src/VFXManagerSpirit.cpp src/PathFinder.cpp src/MonsterManager.cpp src/MonsterManagerRender.cpp src/MonsterManagerEffects.cpp ${IMGUI_SOURCES})
target_link_libraries(BgfxInitTest PRIVATE glfw bgfx bimg bx ${JPEG_LIBRARY} OpenAL::OpenAL)
add_dependencies(BgfxInitTest bgfx_shaders)
if(APPLE)
    target_link_libraries(BgfxInitTest PRIVATE
        "-framework Cocoa" "-framework IOKit" "-framework CoreVideo"
        "-framework QuartzCore" "-framework Metal" "-framework MetalKit")
endif()

# Shader compilation target for BGFX
set(BGFX_SHADER_DIR ${CMAKE_CURRENT_SOURCE_DIR}/shaders/bgfx)
set(BGFX_SHADER_BIN_DIR ${CMAKE_BINARY_DIR}/shaders)
file(MAKE_DIRECTORY ${BGFX_SHADER_BIN_DIR})

# shaderc is built by bgfx.cmake as the "shaderc" target
# Shader compilation function
function(compile_bgfx_shader SHADER_FILE SHADER_TYPE SHADER_MODEL OUTPUT_NAME)
    set(BGFX_SHADERC $<TARGET_FILE:shaderc>)
    set(OUTPUT_FILE ${BGFX_SHADER_BIN_DIR}/${OUTPUT_NAME}.bin)
    set(VARYING_DEF ${BGFX_SHADER_DIR}/varying.def.sc)
    set(INCLUDE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/external/bgfx.cmake/bgfx/src)
    add_custom_command(
        OUTPUT ${OUTPUT_FILE}
        COMMAND ${BGFX_SHADERC}
            -f ${SHADER_FILE}
            -o ${OUTPUT_FILE}
            --type ${SHADER_TYPE}
            --platform osx
            -p ${SHADER_MODEL}
            -i ${INCLUDE_DIR}
            -i ${BGFX_SHADER_DIR}
            --varyingdef ${VARYING_DEF}
        DEPENDS ${SHADER_FILE} ${VARYING_DEF} shaderc
        COMMENT "Compiling BGFX shader: ${OUTPUT_NAME}"
    )
endfunction()

compile_bgfx_shader(${BGFX_SHADER_DIR}/vs_shadow.sc vertex metal vs_shadow)
compile_bgfx_shader(${BGFX_SHADER_DIR}/fs_shadow.sc fragment metal fs_shadow)
compile_bgfx_shader(${BGFX_SHADER_DIR}/vs_depth.sc vertex metal vs_depth)
compile_bgfx_shader(${BGFX_SHADER_DIR}/fs_depth.sc fragment metal fs_depth)
compile_bgfx_shader(${BGFX_SHADER_DIR}/vs_imgui.sc vertex metal vs_imgui)
compile_bgfx_shader(${BGFX_SHADER_DIR}/fs_imgui.sc fragment metal fs_imgui)
compile_bgfx_shader(${BGFX_SHADER_DIR}/vs_model.sc vertex metal vs_model)
compile_bgfx_shader(${BGFX_SHADER_DIR}/fs_model.sc fragment metal fs_model)
compile_bgfx_shader(${BGFX_SHADER_DIR}/vs_sky.sc vertex metal vs_sky)
compile_bgfx_shader(${BGFX_SHADER_DIR}/fs_sky.sc fragment metal fs_sky)
compile_bgfx_shader(${BGFX_SHADER_DIR}/vs_terrain.sc vertex metal vs_terrain)
compile_bgfx_shader(${BGFX_SHADER_DIR}/fs_terrain.sc fragment metal fs_terrain)
compile_bgfx_shader(${BGFX_SHADER_DIR}/vs_grass.sc vertex metal vs_grass)
compile_bgfx_shader(${BGFX_SHADER_DIR}/fs_grass.sc fragment metal fs_grass)
compile_bgfx_shader(${BGFX_SHADER_DIR}/vs_model_skinned.sc vertex metal vs_model_skinned)
compile_bgfx_shader(${BGFX_SHADER_DIR}/vs_billboard.sc vertex metal vs_billboard)
compile_bgfx_shader(${BGFX_SHADER_DIR}/fs_billboard.sc fragment metal fs_billboard)
compile_bgfx_shader(${BGFX_SHADER_DIR}/vs_leaf.sc vertex metal vs_leaf)
compile_bgfx_shader(${BGFX_SHADER_DIR}/fs_leaf.sc fragment metal fs_leaf)
compile_bgfx_shader(${BGFX_SHADER_DIR}/vs_line.sc vertex metal vs_line)
compile_bgfx_shader(${BGFX_SHADER_DIR}/fs_line.sc fragment metal fs_line)
compile_bgfx_shader(${BGFX_SHADER_DIR}/vs_outline.sc vertex metal vs_outline)
compile_bgfx_shader(${BGFX_SHADER_DIR}/fs_outline.sc fragment metal fs_outline)
compile_bgfx_shader(${BGFX_SHADER_DIR}/vs_ground.sc vertex metal vs_ground)
compile_bgfx_shader(${BGFX_SHADER_DIR}/fs_ground.sc fragment metal fs_ground)
compile_bgfx_shader(${BGFX_SHADER_DIR}/vs_postprocess.sc vertex metal vs_postprocess)
compile_bgfx_shader(${BGFX_SHADER_DIR}/fs_bright_extract.sc fragment metal fs_bright_extract)
compile_bgfx_shader(${BGFX_SHADER_DIR}/fs_blur.sc fragment metal fs_blur)
compile_bgfx_shader(${BGFX_SHADER_DIR}/fs_postprocess.sc fragment metal fs_postprocess)

add_custom_target(bgfx_shaders ALL
    DEPENDS
        ${BGFX_SHADER_BIN_DIR}/vs_shadow.bin
        ${BGFX_SHADER_BIN_DIR}/fs_shadow.bin
        ${BGFX_SHADER_BIN_DIR}/vs_depth.bin
        ${BGFX_SHADER_BIN_DIR}/fs_depth.bin
        ${BGFX_SHADER_BIN_DIR}/vs_imgui.bin
        ${BGFX_SHADER_BIN_DIR}/fs_imgui.bin
        ${BGFX_SHADER_BIN_DIR}/vs_model.bin
        ${BGFX_SHADER_BIN_DIR}/fs_model.bin
        ${BGFX_SHADER_BIN_DIR}/vs_sky.bin
        ${BGFX_SHADER_BIN_DIR}/fs_sky.bin
        ${BGFX_SHADER_BIN_DIR}/vs_terrain.bin
        ${BGFX_SHADER_BIN_DIR}/fs_terrain.bin
        ${BGFX_SHADER_BIN_DIR}/vs_grass.bin
        ${BGFX_SHADER_BIN_DIR}/fs_grass.bin
        ${BGFX_SHADER_BIN_DIR}/vs_model_skinned.bin
        ${BGFX_SHADER_BIN_DIR}/vs_billboard.bin
        ${BGFX_SHADER_BIN_DIR}/fs_billboard.bin
        ${BGFX_SHADER_BIN_DIR}/vs_leaf.bin
        ${BGFX_SHADER_BIN_DIR}/fs_leaf.bin
        ${BGFX_SHADER_BIN_DIR}/vs_line.bin
        ${BGFX_SHADER_BIN_DIR}/fs_line.bin
        ${BGFX_SHADER_BIN_DIR}/vs_outline.bin
        ${BGFX_SHADER_BIN_DIR}/fs_outline.bin
        ${BGFX_SHADER_BIN_DIR}/vs_ground.bin
        ${BGFX_SHADER_BIN_DIR}/fs_ground.bin
        ${BGFX_SHADER_BIN_DIR}/vs_postprocess.bin
        ${BGFX_SHADER_BIN_DIR}/fs_bright_extract.bin
        ${BGFX_SHADER_BIN_DIR}/fs_blur.bin
        ${BGFX_SHADER_BIN_DIR}/fs_postprocess.bin
)
add_dependencies(MuRemaster bgfx_shaders)
add_dependencies(ModelViewer bgfx_shaders)
add_dependencies(CharViewer bgfx_shaders)

# Shadow map test executable
add_executable(ShadowTest src/shadow_test.cpp)
target_include_directories(ShadowTest PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/external/bgfx.cmake/bgfx/include
    ${CMAKE_CURRENT_SOURCE_DIR}/external/bgfx.cmake/bx/include
    ${CMAKE_CURRENT_SOURCE_DIR}/external/bgfx.cmake/bimg/include
    ${CMAKE_CURRENT_SOURCE_DIR}/external/glm
    ${CMAKE_CURRENT_SOURCE_DIR}/external/imgui
    ${CMAKE_CURRENT_SOURCE_DIR}/external/stb
)
target_link_libraries(ShadowTest PRIVATE glfw bgfx bimg bx)
add_dependencies(ShadowTest bgfx_shaders)
if(APPLE)
    target_link_libraries(ShadowTest PRIVATE
        "-framework Cocoa" "-framework IOKit" "-framework CoreVideo"
        "-framework QuartzCore" "-framework Metal" "-framework MetalKit")
endif()

# BMD Inspector CLI tool (no rendering deps — just BMDParser + filesystem)
add_executable(BmdInspect ../tools/bmd_inspect.cpp src/BMDParser.cpp src/BMDUtils.cpp)
target_include_directories(BmdInspect PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/external/bgfx.cmake/bx/include
    ${CMAKE_CURRENT_SOURCE_DIR}/external/glm
)
# BMDParser uses glm types — glm is header-only, no link needed
# No BGFX, GLFW, OpenAL, or rendering dependencies

message(STATUS "Configured MuRemaster, ModelViewer, CharViewer, BgfxInitTest, ShadowTest, BmdInspect")
