cmake_minimum_required(VERSION 3.16)
project(Luna VERSION 1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

find_package(Vulkan REQUIRED)
find_package(glfw3 3.3 REQUIRED)
find_package(glm REQUIRED)

add_executable(luna src/main.cpp)

target_link_libraries(luna PRIVATE
    Vulkan::Vulkan
    glfw
    glm::glm
)

find_program(GLSLC glslc HINTS $ENV{VULKAN_SDK}/bin /usr/bin)
if(NOT GLSLC)
    message(FATAL_ERROR "glslc not found! Install glslang-tools.")
endif()

set(SHADER_DIR ${CMAKE_SOURCE_DIR}/shaders)
set(SPIRV_DIR ${CMAKE_BINARY_DIR}/shaders)
file(MAKE_DIRECTORY ${SPIRV_DIR})

set(SHADERS
    ${SHADER_DIR}/shader.vert
    ${SHADER_DIR}/shader.frag
    ${SHADER_DIR}/terrain.vert
    ${SHADER_DIR}/terrain.frag
    ${SHADER_DIR}/stars.vert
    ${SHADER_DIR}/stars.frag
    ${SHADER_DIR}/particles.vert
    ${SHADER_DIR}/particles.frag
    ${SHADER_DIR}/hud.vert
    ${SHADER_DIR}/hud.frag
)

foreach(SHADER ${SHADERS})
    get_filename_component(SHADER_NAME ${SHADER} NAME)
    set(SPIRV_FILE ${SPIRV_DIR}/${SHADER_NAME}.spv)
    add_custom_command(
        OUTPUT ${SPIRV_FILE}
        COMMAND ${GLSLC} ${SHADER} -o ${SPIRV_FILE}
        DEPENDS ${SHADER}
        COMMENT "Compiling ${SHADER_NAME}"
    )
    list(APPEND SPIRV_FILES ${SPIRV_FILE})
endforeach()

add_custom_target(shaders ALL DEPENDS ${SPIRV_FILES})
add_dependencies(luna shaders)

target_compile_definitions(luna PRIVATE SHADER_DIR="${SPIRV_DIR}")
