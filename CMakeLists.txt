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

IF(KVK_DEBUG MATCHES ON)
    message("DEBUG BUILD")
    #set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded")
    add_compile_definitions(KVK_DEBUG)
    #add_compile_options(/Zi)
    set(CMAKE_BUILD_TYPE Debug)
ELSE(KVK_DEBUG MATCHES ON)
    message("RELEASE BUILD")
    #set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded")
    set(CMAKE_BUILD_TYPE Release)
    set(CMAKE_INTERPROCEDURAL_OPTIMIZATION TRUE)
    IF(KAMSKI_LLVM MATCHES ON)
        add_compile_options(-Ofast)
        #    ELSE()
        #add_compile_options(/O2 /fp:fast)
    ENDIF()
ENDIF(KVK_DEBUG MATCHES ON)

###########################################################################################
# Dependencies
###########################################################################################
add_subdirectory(vma)
add_subdirectory(glm)
if(DEFINED PROFILE)
    add_subdirectory(tracy)
endif()

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

target_link_directories(kamskiVk PUBLIC $ENV{VULKAN_SDK}/Lib/)
if (WIN32)
    target_link_libraries(kamskiVk PUBLIC vulkan-1 VulkanMemoryAllocator glm-header-only)
else ()
    target_link_libraries(kamskiVk PUBLIC vulkan VulkanMemoryAllocator glm-header-only)
endif ()

if(DEFINED PROFILE)
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

target_precompile_headers(kamskiVk PUBLIC
 "$<$<COMPILE_LANGUAGE:CXX>:deque>"
 "$<$<COMPILE_LANGUAGE:CXX>:thread>"
 "$<$<COMPILE_LANGUAGE:CXX>:mutex>"
 "$<$<COMPILE_LANGUAGE:CXX>:atomic>"
 "$<$<COMPILE_LANGUAGE:CXX>:algorithm>"
 "$<$<COMPILE_LANGUAGE:CXX>:vector>"
 "$<$<COMPILE_LANGUAGE:CXX>:span>"
 "$<$<COMPILE_LANGUAGE:CXX>:set>"
 "$<$<COMPILE_LANGUAGE:CXX>:numeric>"
 "$<$<COMPILE_LANGUAGE:CXX>:fstream>"
)

###########################################################################################
# Shader Compilation
###########################################################################################
if(WIN32)
    if (${CMAKE_HOST_SYSTEM_PROCESSOR} STREQUAL "AMD64")
        set(GLSL_VALIDATOR "$ENV{VULKAN_SDK}/Bin/glslangValidator.exe" PARENT_SCOPE)
        set(GLSL_VALIDATOR "$ENV{VULKAN_SDK}/Bin/glslangValidator.exe")
    else()
        set(GLSL_VALIDATOR "$ENV{VULKAN_SDK}/Bin32/glslangValidator.exe" PARENT_SCOPE)
        set(GLSL_VALIDATOR "$ENV{VULKAN_SDK}/Bin32/glslangValidator.exe")
    endif()
else()
    if (${CMAKE_HOST_SYSTEM_PROCESSOR} STREQUAL "AMD64")
        set(GLSL_VALIDATOR "glslangValidator" PARENT_SCOPE)
        set(GLSL_VALIDATOR "glslangValidator")
    else()
        set(GLSL_VALIDATOR "glslangValidator" PARENT_SCOPE)
        set(GLSL_VALIDATOR "glslangValidator")
    endif()
endif()

function(addShaders target custom_target SHADERS)
    foreach(GLSL ${SHADERS})
        get_filename_component(FILE_NAME ${GLSL} NAME)
        set(SPIRV "${PROJECT_BINARY_DIR}/shaders/${FILE_NAME}.spv")
        add_custom_command(
   OUTPUT ${SPIRV}
   COMMAND ${CMAKE_COMMAND} -E make_directory "${PROJECT_BINARY_DIR}/shaders/"
   COMMAND ${GLSL_VALIDATOR} -V ${GLSL} -o ${SPIRV}
   DEPENDS ${GLSL})
        list(APPEND BINARY_LIST ${SPIRV})
    endforeach(GLSL)
    add_custom_target(${custom_target} DEPENDS ${BINARY_LIST})
    add_dependencies(${target} ${custom_target})
endfunction()

###########################################################################################
# Test compilation
###########################################################################################
if(DEFINED BUILD_TESTS)
    message("Building tests")
    add_subdirectory(test)
else()
    message("Not building tests")
endif()
