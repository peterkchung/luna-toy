cmake_minimum_required(VERSION 3.22)
project(Luna VERSION 1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(Vulkan REQUIRED)
find_package(glfw3 3.3 REQUIRED)
find_package(glm REQUIRED)

add_executable(luna src/main.cpp)

target_link_libraries(luna PRIVATE
    Vulkan::Vulkan
    glfw
    glm::glm
)
