//go:build cgo

package envoy

/*
#include "abi.h"
*/
import "C"

//export envoy_dynamic_module_on_program_init
func envoy_dynamic_module_on_program_init() C.envoy_dynamic_module_type_program_init_result {
	if OnProgramInit == nil {
		return 0
	}
	return C.envoy_dynamic_module_type_program_init_result(OnProgramInit())
}
