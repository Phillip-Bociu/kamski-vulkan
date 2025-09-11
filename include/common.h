#pragma once
#include "../../../src/KamskiEngine/KamskiTypes.h"
#include <stdio.h>
#include <cstdint>

namespace kvk {

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
