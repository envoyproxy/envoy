package main

import "C"
import "unsafe"

func main() {} // c-shared must define the empty main function.

var version = []byte("invalid-version-hash\000")

//export envoy_dynamic_module_on_program_init
func envoy_dynamic_module_on_program_init() uintptr {
	return uintptr(unsafe.Pointer(&version[0]))
}
