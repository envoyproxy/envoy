package abi

/*
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include "../../../abi/abi.h"
*/
import "C"

import (
	"runtime"
	"unsafe"

	sdk "github.com/envoyproxy/envoy/source/extensions/dynamic_modules/sdk/go"
	"github.com/envoyproxy/envoy/source/extensions/dynamic_modules/sdk/go/shared"
)

////////////////////////////////////////////////////////////////////////////////////////////////////
// Early Header Mutation
////////////////////////////////////////////////////////////////////////////////////////////////////

// earlyHeaderMutationWrapper holds the per-config Go state that must stay alive for as long as
// Envoy keeps the in-module config pointer. It is kept alive by earlyHeaderMutationConfigManager,
// which also maps the pointer back to the wrapper.
type earlyHeaderMutationWrapper struct {
	mutation     shared.EarlyHeaderMutation
	configHandle *dymEarlyHeaderMutationConfigHandle
}

// earlyHeaderMutationConfigManager keeps each earlyHeaderMutationWrapper alive for as long as
// Envoy holds the in-module config pointer and maps that pointer back to the wrapper. unwrap is
// lock-free, which matters because on_early_header_mutation_mutate runs on worker threads for
// every request.
var earlyHeaderMutationConfigManager = newManager[earlyHeaderMutationWrapper]()

// dymEarlyHeaderMutationHeaderMap implements shared.HeaderMap over the request headers of one
// early header mutation. Only the request headers exist at this point in the request lifecycle,
// so the underlying callbacks take no header type.
type dymEarlyHeaderMutationHeaderMap struct {
	hostPtr C.envoy_dynamic_module_type_early_header_mutation_context_envoy_ptr
}

func (h *dymEarlyHeaderMutationHeaderMap) getSingleHeader(
	key string, index uint64, valueCount *uint64,
) shared.UnsafeEnvoyBuffer {
	var valueView C.envoy_dynamic_module_type_envoy_buffer
	ret := C.envoy_dynamic_module_callback_early_header_mutation_get_header_value(
		h.hostPtr,
		stringToModuleBuffer(key),
		&valueView,
		(C.size_t)(index),
		(*C.size_t)(valueCount),
	)
	runtime.KeepAlive(key)

	if !bool(ret) || valueView.ptr == nil || valueView.length == 0 {
		return shared.UnsafeEnvoyBuffer{}
	}
	return envoyBufferToUnsafeEnvoyBuffer(valueView)
}

func (h *dymEarlyHeaderMutationHeaderMap) Get(key string) []shared.UnsafeEnvoyBuffer {
	valueCount := uint64(0)

	firstValue := h.getSingleHeader(key, 0, &valueCount)
	if valueCount == 0 {
		return []shared.UnsafeEnvoyBuffer{}
	}

	values := make([]shared.UnsafeEnvoyBuffer, 0, valueCount)
	values = append(values, firstValue)

	for i := uint64(1); i < valueCount; i++ {
		values = append(values, h.getSingleHeader(key, i, nil))
	}
	return values
}

func (h *dymEarlyHeaderMutationHeaderMap) GetOne(key string) shared.UnsafeEnvoyBuffer {
	return h.getSingleHeader(key, 0, nil)
}

func (h *dymEarlyHeaderMutationHeaderMap) GetAll() [][2]shared.UnsafeEnvoyBuffer {
	headerCount := C.envoy_dynamic_module_callback_early_header_mutation_get_headers_size(h.hostPtr)
	if headerCount == 0 {
		return nil
	}

	resultHeaders := make([]C.envoy_dynamic_module_type_envoy_http_header, headerCount)
	if !bool(C.envoy_dynamic_module_callback_early_header_mutation_get_headers(
		h.hostPtr,
		unsafe.SliceData(resultHeaders),
	)) {
		return nil
	}
	finalResult := envoyHttpHeaderSliceToUnsafeHeaderSlice(resultHeaders)
	runtime.KeepAlive(resultHeaders)
	return finalResult
}

func (h *dymEarlyHeaderMutationHeaderMap) Set(key, value string) {
	C.envoy_dynamic_module_callback_early_header_mutation_set_header(
		h.hostPtr,
		stringToModuleBuffer(key),
		stringToModuleBuffer(value),
	)
	runtime.KeepAlive(key)
	runtime.KeepAlive(value)
}

func (h *dymEarlyHeaderMutationHeaderMap) Add(key, value string) {
	C.envoy_dynamic_module_callback_early_header_mutation_add_header(
		h.hostPtr,
		stringToModuleBuffer(key),
		stringToModuleBuffer(value),
	)
	runtime.KeepAlive(key)
	runtime.KeepAlive(value)
}

func (h *dymEarlyHeaderMutationHeaderMap) Remove(key string) {
	// Unlike the HTTP filter header map, the early header mutation ABI has a dedicated remove
	// callback rather than overloading the setter with a null value.
	C.envoy_dynamic_module_callback_early_header_mutation_remove_header(
		h.hostPtr,
		stringToModuleBuffer(key),
	)
	runtime.KeepAlive(key)
}

// dymEarlyHeaderMutationHandle implements shared.EarlyHeaderMutationHandle. One is created per
// request because the Envoy pointer it wraps is only valid for the duration of a single mutate
// call.
type dymEarlyHeaderMutationHandle struct {
	hostPtr        C.envoy_dynamic_module_type_early_header_mutation_context_envoy_ptr
	requestHeaders dymEarlyHeaderMutationHeaderMap
}

func newDymEarlyHeaderMutationHandle(
	hostPtr C.envoy_dynamic_module_type_early_header_mutation_context_envoy_ptr,
) *dymEarlyHeaderMutationHandle {
	return &dymEarlyHeaderMutationHandle{
		hostPtr:        hostPtr,
		requestHeaders: dymEarlyHeaderMutationHeaderMap{hostPtr: hostPtr},
	}
}

func (h *dymEarlyHeaderMutationHandle) RequestHeaders() shared.HeaderMap {
	return &h.requestHeaders
}

func (h *dymEarlyHeaderMutationHandle) GetAttributeString(
	attributeID shared.AttributeID,
) (shared.UnsafeEnvoyBuffer, bool) {
	var result C.envoy_dynamic_module_type_envoy_buffer
	if !bool(C.envoy_dynamic_module_callback_early_header_mutation_get_attribute_string(
		h.hostPtr,
		(C.envoy_dynamic_module_type_attribute_id)(uint32(attributeID)),
		&result,
	)) {
		return shared.UnsafeEnvoyBuffer{}, false
	}
	return envoyBufferToUnsafeEnvoyBuffer(result), true
}

func (h *dymEarlyHeaderMutationHandle) GetAttributeInt(
	attributeID shared.AttributeID,
) (uint64, bool) {
	var result C.uint64_t
	if !bool(C.envoy_dynamic_module_callback_early_header_mutation_get_attribute_int(
		h.hostPtr,
		(C.envoy_dynamic_module_type_attribute_id)(uint32(attributeID)),
		&result,
	)) {
		return 0, false
	}
	return uint64(result), true
}

func (h *dymEarlyHeaderMutationHandle) GetAttributeBool(
	attributeID shared.AttributeID,
) (bool, bool) {
	var result C.bool
	if !bool(C.envoy_dynamic_module_callback_early_header_mutation_get_attribute_bool(
		h.hostPtr,
		(C.envoy_dynamic_module_type_attribute_id)(uint32(attributeID)),
		&result,
	)) {
		return false, false
	}
	return bool(result), true
}

func (h *dymEarlyHeaderMutationHandle) GetDynamicMetadataString(
	filterName, path string,
) (shared.UnsafeEnvoyBuffer, bool) {
	var result C.envoy_dynamic_module_type_envoy_buffer
	ret := C.envoy_dynamic_module_callback_early_header_mutation_get_dynamic_metadata(
		h.hostPtr,
		stringToModuleBuffer(filterName),
		stringToModuleBuffer(path),
		&result,
	)
	runtime.KeepAlive(filterName)
	runtime.KeepAlive(path)
	if !bool(ret) {
		return shared.UnsafeEnvoyBuffer{}, false
	}
	return envoyBufferToUnsafeEnvoyBuffer(result), true
}

func (h *dymEarlyHeaderMutationHandle) GetDynamicMetadataNumber(
	filterName, path string,
) (float64, bool) {
	var result C.double
	ret := C.envoy_dynamic_module_callback_early_header_mutation_get_dynamic_metadata_number(
		h.hostPtr,
		stringToModuleBuffer(filterName),
		stringToModuleBuffer(path),
		&result,
	)
	runtime.KeepAlive(filterName)
	runtime.KeepAlive(path)
	if !bool(ret) {
		return 0, false
	}
	return float64(result), true
}

func (h *dymEarlyHeaderMutationHandle) GetDynamicMetadataBool(
	filterName, path string,
) (bool, bool) {
	var result C.bool
	ret := C.envoy_dynamic_module_callback_early_header_mutation_get_dynamic_metadata_bool(
		h.hostPtr,
		stringToModuleBuffer(filterName),
		stringToModuleBuffer(path),
		&result,
	)
	runtime.KeepAlive(filterName)
	runtime.KeepAlive(path)
	if !bool(ret) {
		return false, false
	}
	return bool(result), true
}

func (h *dymEarlyHeaderMutationHandle) GetFilterState(
	key string,
) (shared.UnsafeEnvoyBuffer, bool) {
	var result C.envoy_dynamic_module_type_envoy_buffer
	ret := C.envoy_dynamic_module_callback_early_header_mutation_get_filter_state_bytes(
		h.hostPtr,
		stringToModuleBuffer(key),
		&result,
	)
	runtime.KeepAlive(key)
	if !bool(ret) {
		return shared.UnsafeEnvoyBuffer{}, false
	}
	return envoyBufferToUnsafeEnvoyBuffer(result), true
}

func (h *dymEarlyHeaderMutationHandle) Log(level shared.LogLevel, format string, args ...any) {
	hostLog(level, format, args)
}

func (h *dymEarlyHeaderMutationHandle) GetLogLevel() shared.LogLevel {
	return shared.LogLevel(C.envoy_dynamic_module_callback_get_log_level())
}

func (h *dymEarlyHeaderMutationHandle) IsLogLevelEnabled(level shared.LogLevel) bool {
	return bool(C.envoy_dynamic_module_callback_log_enabled(
		(C.envoy_dynamic_module_type_log_level)(uint32(level)),
	))
}

// dymEarlyHeaderMutationConfigHandle implements shared.EarlyHeaderMutationConfigHandle. Early
// header mutation exposes no config-scoped callbacks yet, so it only carries the Envoy-side
// configuration pointer for future use.
type dymEarlyHeaderMutationConfigHandle struct {
	hostConfigPtr C.envoy_dynamic_module_type_early_header_mutation_config_envoy_ptr
}

func (h *dymEarlyHeaderMutationConfigHandle) Log(
	level shared.LogLevel, format string, args ...any,
) {
	hostLog(level, format, args)
}

func (h *dymEarlyHeaderMutationConfigHandle) GetLogLevel() shared.LogLevel {
	return shared.LogLevel(C.envoy_dynamic_module_callback_get_log_level())
}

func (h *dymEarlyHeaderMutationConfigHandle) IsLogLevelEnabled(level shared.LogLevel) bool {
	return bool(C.envoy_dynamic_module_callback_log_enabled(
		(C.envoy_dynamic_module_type_log_level)(uint32(level)),
	))
}

//export envoy_dynamic_module_on_early_header_mutation_config_new
func envoy_dynamic_module_on_early_header_mutation_config_new(
	hostConfigPtr C.envoy_dynamic_module_type_early_header_mutation_config_envoy_ptr,
	name C.envoy_dynamic_module_type_envoy_buffer,
	config C.envoy_dynamic_module_type_envoy_buffer,
) C.envoy_dynamic_module_type_early_header_mutation_config_module_ptr {
	nameString := envoyBufferToStringUnsafe(name)
	configBytes := envoyBufferToBytesUnsafe(config)

	configHandle := &dymEarlyHeaderMutationConfigHandle{hostConfigPtr: hostConfigPtr}
	mutation, err := sdk.NewEarlyHeaderMutation(configHandle, nameString, configBytes)
	if err != nil {
		configHandle.Log(shared.LogLevelWarn,
			"Failed to load early header mutation configuration for %q: %v", nameString, err)
		return nil
	}
	if mutation == nil {
		configHandle.Log(shared.LogLevelWarn,
			"Failed to load early header mutation configuration for %q: mutation is nil", nameString)
		return nil
	}

	configPtr := earlyHeaderMutationConfigManager.record(&earlyHeaderMutationWrapper{
		mutation:     mutation,
		configHandle: configHandle,
	})
	return C.envoy_dynamic_module_type_early_header_mutation_config_module_ptr(configPtr)
}

//export envoy_dynamic_module_on_early_header_mutation_config_destroy
func envoy_dynamic_module_on_early_header_mutation_config_destroy(
	configPtr C.envoy_dynamic_module_type_early_header_mutation_config_module_ptr,
) {
	wrapper := earlyHeaderMutationConfigManager.unwrap(unsafe.Pointer(configPtr))
	if wrapper == nil {
		return
	}
	wrapper.mutation.OnDestroy()
	earlyHeaderMutationConfigManager.remove(unsafe.Pointer(configPtr))
}

//export envoy_dynamic_module_on_early_header_mutation_mutate
func envoy_dynamic_module_on_early_header_mutation_mutate(
	configPtr C.envoy_dynamic_module_type_early_header_mutation_config_module_ptr,
	hostPtr C.envoy_dynamic_module_type_early_header_mutation_context_envoy_ptr,
) C.bool {
	wrapper := earlyHeaderMutationConfigManager.unwrap(unsafe.Pointer(configPtr))
	if wrapper == nil {
		// The return value selects chain continuation, not success, so a missing mutation must not
		// suppress the extensions configured after this one.
		return C.bool(true)
	}

	// The handle is the only per-request state: it is created per call because the Envoy pointer it
	// wraps is invalidated once the call returns.
	handle := newDymEarlyHeaderMutationHandle(hostPtr)
	return C.bool(wrapper.mutation.Mutate(handle.RequestHeaders(), handle))
}
