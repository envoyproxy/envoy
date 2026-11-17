package abi

/*
#include <stdbool.h>
#include <stdint.h>
#include "../../../abi/abi.h"
*/
import "C"

import "github.com/envoyproxy/envoy/source/extensions/dynamic_modules/sdk/go/shared"

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

// dymLoadShedPoint implements shared.LoadShedPoint against the C ABI.
type dymLoadShedPoint struct {
	hostLoadShedPointPtr C.envoy_dynamic_module_type_load_shed_point_envoy_ptr
}

func (p *dymLoadShedPoint) ShouldShedLoad() bool {
	return bool(C.envoy_dynamic_module_callback_load_shed_point_should_shed_load(
		p.hostLoadShedPointPtr,
	))
}

// newLoadShedPoint wraps a load shed point that a get_load_shed_point callback returned. It returns
// a nil interface rather than a nil pointer for a null load shed point.
func newLoadShedPoint(
	hostLoadShedPointPtr C.envoy_dynamic_module_type_load_shed_point_envoy_ptr,
) shared.LoadShedPoint {
	if hostLoadShedPointPtr == nil {
		return nil
	}
	return &dymLoadShedPoint{hostLoadShedPointPtr: hostLoadShedPointPtr}
}
