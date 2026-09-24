package abi

/*
#include <stdbool.h>
#include <stdint.h>
#include "../../../abi/abi.h"
*/
import "C"

// dymCommonHandle implements shared.CommonHandle against the C ABI. It is zero sized and is
// embedded into every config handle type, so each of them gains the process-wide callbacks without
// restating them and without growing.
type dymCommonHandle struct{}

func (dymCommonHandle) GetRuntimeBool(key string, defaultValue bool) bool {
	return bool(C.envoy_dynamic_module_callback_get_runtime_bool(
		stringToModuleBuffer(key), C.bool(defaultValue),
	))
}

func (dymCommonHandle) GetRuntimeInt(key string, defaultValue uint64) uint64 {
	return uint64(C.envoy_dynamic_module_callback_get_runtime_int(
		stringToModuleBuffer(key), C.uint64_t(defaultValue),
	))
}

func (dymCommonHandle) GetRuntimeNumber(key string, defaultValue float64) float64 {
	return float64(C.envoy_dynamic_module_callback_get_runtime_number(
		stringToModuleBuffer(key), C.double(defaultValue),
	))
}
