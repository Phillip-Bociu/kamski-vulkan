cmake_minimum_required(VERSION 3.23)

file(GLOB_RECURSE TEST_CPP_FILES "*cpp")
file(GLOB_RECURSE GLSL_SOURCE_FILES "${CMAKE_SOURCE_DIR}/shaders/*.glsl")

add_executable(kamskiVkTest ${TEST_CPP_FILES})
target_link_libraries(kamskiVkTest PUBLIC kamskiVk)

addShaders(kamskiVkTest "${GLSL_SOURCE_FILES}")

