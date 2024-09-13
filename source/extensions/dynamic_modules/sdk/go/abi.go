//go:build cgo

package gosdk

/*
#define ENVOY_DYNAMIC_MODULE_GO_SDK 1
#include "abi.h"
*/
import "C"
import (
	"fmt"
	"unsafe"
)

// abiVersion is the null-terminated magic number that must be returned by the on_program_init function.
var abiVersion = []byte("4613e2f0b4da7a99a65f578137207449085f4017160ea5818bb54fd8c4f11187\000")

//export envoy_dynamic_module_on_program_init
func envoy_dynamic_module_on_program_init() C.envoy_dynamic_module_type_abi_version {
	if OnProgramInit() {
		fmt.Println("OnProgramInit() returned true")
		return C.envoy_dynamic_module_type_abi_version(uintptr(unsafe.Pointer(&abiVersion[0])))
	}
	fmt.Println("OnProgramInit() returned false")
	return 0
}
