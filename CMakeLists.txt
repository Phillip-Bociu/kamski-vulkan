cmake_minimum_required(VERSION 3.23)
cmake_policy(SET CMP0091 NEW)
project(kvk)
find_package(Vulkan REQUIRED)

# some settings

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_EXPORT_COMPILE_COMMANDS true)
set(CMAKE_TRY_COMPILE_TARGET_TYPE "STATIC_LIBRARY")

IF(KVK_DEBUG MATCHES ON)
	message("DEBUG BUILD")
	set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded")
	add_compile_definitions(KVK_DEBUG)
	set(CMAKE_BUILD_TYPE Debug)
ELSE(KVK_DEBUG MATCHES ON)
	message("RELEASE BUILD")
	set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded")
	set(CMAKE_BUILD_TYPE Release)
	set(CMAKE_INTERPROCEDURAL_OPTIMIZATION TRUE)
	IF(KAMSKI_LLVM MATCHES ON)
		add_compile_options(-Ofast)
	ELSE()
		add_compile_options(/O2 /fp:fast)
	ENDIF()

ENDIF(KVK_DEBUG MATCHES ON)

# Globs

file(GLOB_RECURSE LIB_CPP_FILES "src/*cpp")
file(GLOB_RECURSE TEST_CPP_FILES "test/*cpp")
file(GLOB_RECURSE GLSL_SOURCE_FILES
	"${CMAKE_SOURCE_DIR}/src/shaders/*.frag"
	"${CMAKE_SOURCE_DIR}/src/shaders/*.vert")


# Renderer library

add_library(kamskiVk STATIC ${LIB_CPP_FILES})
target_link_directories(kamskiVk PUBLIC $ENV{VULKAN_SDK}/Lib/)
target_link_libraries(kamskiVk PUBLIC vulkan-1)
target_include_directories(kamskiVk PUBLIC ${CMAKE_SOURCE_DIR}/include/ $ENV{VULKAN_SDK}/Include/)

# SPIR-V compilation
if (${CMAKE_HOST_SYSTEM_PROCESSOR} STREQUAL "AMD64")
  set(GLSL_VALIDATOR "$ENV{VULKAN_SDK}/Bin/glslangValidator.exe")
else()
  set(GLSL_VALIDATOR "$ENV{VULKAN_SDK}/Bin32/glslangValidator.exe")
endif()

foreach(GLSL ${GLSL_SOURCE_FILES})
  get_filename_component(FILE_NAME ${GLSL} NAME)
  set(SPIRV "${PROJECT_BINARY_DIR}/shaders/${FILE_NAME}.spv")
  add_custom_command(
    OUTPUT ${SPIRV}
    COMMAND ${CMAKE_COMMAND} -E make_directory "${PROJECT_BINARY_DIR}/shaders/"
    COMMAND ${GLSL_VALIDATOR} -V ${GLSL} -o ${SPIRV}
    DEPENDS ${GLSL})
  list(APPEND SPIRV_BINARY_FILES ${SPIRV})
endforeach(GLSL)

add_custom_target(
    Shaders 
    DEPENDS ${SPIRV_BINARY_FILES}
    )
add_dependencies(kamskiVk Shaders)

# Test executable

if(BUILD_TESTS EQUAL 1)
	message("Building tests")
	add_executable(kamskiVkTest ${TEST_CPP_FILES})
	target_link_libraries(kamskiVkTest PUBLIC kamskiVk)
else()
	message("Not building tests")
endif()

