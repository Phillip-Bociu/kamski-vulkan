cmake_minimum_required(VERSION 3.23)
cmake_policy(SET CMP0091 NEW)
project(kvk)

find_package(Vulkan)

###########################################################################################
# Flags
###########################################################################################
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_EXPORT_COMPILE_COMMANDS true)
set(CMAKE_TRY_COMPILE_TARGET_TYPE "STATIC_LIBRARY")

IF(DEFINED KAMSKI_DEBUG)
	message("DEBUG BUILD")
	set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded")
	add_compile_definitions(KAMSKI_DEBUG)
	set(CMAKE_BUILD_TYPE Debug)
ELSE()
	message("RELEASE BUILD")
	set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded")
	set(CMAKE_BUILD_TYPE Release)
	set(CMAKE_INTERPROCEDURAL_OPTIMIZATION TRUE)
	IF(DEFINED KAMSKI_LLVM)
		add_compile_options(-Ofast)
	ELSE()
		add_compile_options(/O2 /fp:fast)
	ENDIF()
ENDIF()

###########################################################################################
# Dependencies
###########################################################################################
add_subdirectory(vma)
add_subdirectory(glm)
if(DEFINED PROFILE)
    add_subdirectory(tracy)
endif()

# SPIR-V Reflection
add_library(spirv_reflect STATIC "${CMAKE_CURRENT_SOURCE_DIR}/spirv-reflect/spirv_reflect.c")
target_include_directories(spirv_reflect PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/spirv-reflect")

###########################################################################################
# Globs
###########################################################################################
file(GLOB_RECURSE WIN_LIB_CPP_FILES "src/*_win32*cpp")

###########################################################################################
# Renderer library
###########################################################################################

add_library(kamskiVk STATIC)
target_sources(kamskiVk PRIVATE src/krender.cpp src/utils.cpp)

if(NOT DEFINED KVK_GLFW)
    if(WIN32)
        target_sources(kamskiVk PRIVATE "${WIN_LIB_CPP_FILES}")
    endif()
else()
    target_compile_definitions(kamskiVk PUBLIC KVK_GLFW)
    target_link_libraries(kamskiVk PUBLIC glfw)
endif()
target_compile_options(kamskiVk PUBLIC /Zi)

target_link_directories(kamskiVk PUBLIC $ENV{VULKAN_SDK}/Lib/)
if (WIN32)
    target_link_libraries(kamskiVk PUBLIC vulkan-1)
else ()
    target_link_libraries(kamskiVk PUBLIC vulkan)
endif ()

target_link_libraries(kamskiVk PUBLIC spirv_reflect VulkanMemoryAllocator glm-header-only)


if(DEFINED KAMSKI_PROFILE)
    message("PROFILER ENABLED")
	option( TRACY_ENABLE "" ON)
	option( TRACY_ON_DEMAND "" ON)
    target_compile_definitions(kamskiVk PUBLIC PROFILER_ENABLED)
    target_link_libraries(kamskiVk PUBLIC TracyClient)
ELSE()
	message("PROFILER DISABLED")
	option( TRACY_ENABLE "" OFF)
	option( TRACY_ON_DEMAND "" ON)
endif()

target_include_directories(kamskiVk PUBLIC ./include/ $ENV{VULKAN_SDK}/Include/)

###########################################################################################
# Test compilation
###########################################################################################
if(DEFINED BUILD_TESTS)
    message("Building tests")
    add_subdirectory(test)
else()
    message("Not building tests")
endif()
