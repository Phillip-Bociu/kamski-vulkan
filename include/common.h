#pragma once
#include <stdio.h>

namespace kvk {
#if defined(KVK_DEBUG) && !defined(logDebug)
#define logDebug(format, ...)   printf("[DEBUG][%5d]: " __FILE__ ": " format "\n", __LINE__, __VA_ARGS__)
#define logInfo(format, ...)    printf("[INFO] [%5d]: " __FILE__ ": " format "\n", __LINE__, __VA_ARGS__)
#define logWarning(format, ...) printf("[WARN] [%5d]: " __FILE__ ": " format "\n", __LINE__, __VA_ARGS__)
#define logError(format, ...)   printf("[ERROR][%5d]: " __FILE__ ": " format "\n", __LINE__, __VA_ARGS__)
#endif

enum class [[nodiscard]] ReturnCode {
	OK,
	WRONG_PARAMETERS,
	LAYER_NOT_FOUND,
	DEVICE_NOT_FOUND,
	QFAM_NOT_FOUND,
	SHADER_CREATION_ERROR,
	FILE_NOT_FOUND,
	UNKNOWN,
	COUNT,
};

}
