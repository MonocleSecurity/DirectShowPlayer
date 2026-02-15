cmake_minimum_required(VERSION 3.21)

set(CMAKE_TOOLCHAIN_FILE ${CMAKE_CURRENT_SOURCE_DIR}/vcpkg/scripts/buildsystems/vcpkg.cmake CACHE STRING "Vcpkg toolchain file")

project(DirectShowPlayer C CXX)

set(CMAKE_CXX_STANDARD 17)

find_package(GLEW REQUIRED)
find_package(glfw3 CONFIG REQUIRED)

add_executable(DirectShowPlayer
main.cpp)

target_link_libraries(DirectShowPlayer PRIVATE GLEW::GLEW)
target_link_libraries(DirectShowPlayer PRIVATE glfw)
target_link_libraries(DirectShowPlayer PRIVATE strmiids.lib)

if(WIN32)
  target_link_libraries(DirectShowPlayer PRIVATE opengl32)
endif()
