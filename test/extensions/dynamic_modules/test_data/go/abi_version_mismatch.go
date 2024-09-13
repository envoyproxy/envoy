package main

import "C"
import "unsafe"

func main() {} // c-shared must define the empty main function.

//export envoy_dynamic_module_on_program_init
func envoy_dynamic_module_on_program_init() uintptr {
	version := append([]byte("invalid-version-hash"), 0)
	return uintptr(unsafe.Pointer(&version[0]))
}
