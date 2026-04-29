package abi

/*
#cgo darwin LDFLAGS: -Wl,-undefined,dynamic_lookup
#cgo linux LDFLAGS: -Wl,--unresolved-symbols=ignore-all
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include "../../../abi/abi.h"
*/
import "C"
import (
	"runtime"
	"unsafe"

	sdk "github.com/envoyproxy/envoy/source/extensions/dynamic_modules/sdk/go"
	"github.com/envoyproxy/envoy/source/extensions/dynamic_modules/sdk/go/shared"
)

// init wires the live ABI-backed program handle into the sdk package so that
// sdk.GetConcurrency / sdk.IsValidationMode / sdk.Register*/Get* are usable from any module
// code that imports the abi package (which all modules do transitively).
func init() {
	sdk.SetProgramHandle(DefaultProgramHandle)
}

// dymProgramHandle implements shared.ProgramHandle by calling into Envoy via the
// dynamic-modules ABI.
type dymProgramHandle struct{}

// DefaultProgramHandle is the singleton ProgramHandle backed by the live Envoy ABI. The sdk
// package exposes its methods via top-level convenience functions; modules under test can
// substitute their own shared.ProgramHandle implementation.
var DefaultProgramHandle shared.ProgramHandle = &dymProgramHandle{}

func (dymProgramHandle) GetConcurrency() uint32 {
	return uint32(C.envoy_dynamic_module_callback_get_concurrency())
}

func (dymProgramHandle) IsValidationMode() bool {
	return bool(C.envoy_dynamic_module_callback_is_validation_mode())
}

func (dymProgramHandle) RegisterFunction(key string, fnPtr unsafe.Pointer) bool {
	ret := C.envoy_dynamic_module_callback_register_function(
		stringToModuleBuffer(key), fnPtr)
	runtime.KeepAlive(key)
	return bool(ret)
}

func (dymProgramHandle) GetFunction(key string) (unsafe.Pointer, bool) {
	var p unsafe.Pointer
	ret := C.envoy_dynamic_module_callback_get_function(
		stringToModuleBuffer(key), &p)
	runtime.KeepAlive(key)
	if !bool(ret) {
		return nil, false
	}
	return p, true
}

func (dymProgramHandle) RegisterSharedData(key string, dataPtr unsafe.Pointer) bool {
	ret := C.envoy_dynamic_module_callback_register_shared_data(
		stringToModuleBuffer(key), dataPtr)
	runtime.KeepAlive(key)
	return bool(ret)
}

func (dymProgramHandle) GetSharedData(key string) (unsafe.Pointer, bool) {
	var p unsafe.Pointer
	ret := C.envoy_dynamic_module_callback_get_shared_data(
		stringToModuleBuffer(key), &p)
	runtime.KeepAlive(key)
	if !bool(ret) {
		return nil, false
	}
	return p, true
}

func (dymProgramHandle) Log(level shared.LogLevel, format string, args ...any) {
	hostLog(level, format, args)
}
