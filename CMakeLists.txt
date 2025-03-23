cmake_minimum_required(VERSION 3.23)
cmake_policy(SET CMP0091 NEW)
project(kvk)
find_package(Vulkan REQUIRED)

###########################################################################################
# Flags
###########################################################################################
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_EXPORT_COMPILE_COMMANDS true)
set(CMAKE_TRY_COMPILE_TARGET_TYPE "STATIC_LIBRARY")

IF(KVK_DEBUG MATCHES ON)
	message("DEBUG BUILD")
	set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded")
	add_compile_definitions(KVK_DEBUG)
	add_compile_options(/Zi)
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

###########################################################################################
# Dependencies
###########################################################################################
add_subdirectory(vma)
add_subdirectory(glm)
add_subdirectory(fastgltf)

###########################################################################################
# Globs
###########################################################################################
file(GLOB_RECURSE LIB_CPP_FILES "src/*cpp")

###########################################################################################
# Renderer library
###########################################################################################
add_library(kamskiVk STATIC ${LIB_CPP_FILES})
target_link_directories(kamskiVk PUBLIC $ENV{VULKAN_SDK}/Lib/)
target_link_libraries(kamskiVk PUBLIC vulkan-1 VulkanMemoryAllocator glm-header-only fastgltf)
target_include_directories(kamskiVk PUBLIC ${CMAKE_SOURCE_DIR}/include/ $ENV{VULKAN_SDK}/Include/)
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
if (${CMAKE_HOST_SYSTEM_PROCESSOR} STREQUAL "AMD64")
  set(GLSL_VALIDATOR "$ENV{VULKAN_SDK}/Bin/glslangValidator.exe")
else()
  set(GLSL_VALIDATOR "$ENV{VULKAN_SDK}/Bin32/glslangValidator.exe")
endif()

function(addShaders target SHADERS)
	message(${SHADERS})
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
	add_custom_target(Shaders DEPENDS ${BINARY_LIST})
	add_dependencies(${target} Shaders)
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

