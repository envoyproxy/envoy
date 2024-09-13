//go:build cgo

package gosdk

/*
#define ENVOY_DYNAMIC_MODULE_GO_SDK 1
#include "abi.h"
*/
import "C"
import "unsafe"

var abiVersion = append([]byte(string("7bf4504e9874e385f15c4a835da3c4dfe9480a3d7262d46b18740d7192866649")), 0)

//export envoy_dynamic_module_on_program_init
func envoy_dynamic_module_on_program_init() C.envoy_dynamic_module_type_abi_version {
	if OnProgramInit() {
		return C.envoy_dynamic_module_type_abi_version(uintptr(unsafe.Pointer(&abiVersion[0])))
	}
	return 0
}
