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

type matcherConfigWrapper struct {
	matcher shared.Matcher
}

var matcherConfigManager = newManager[matcherConfigWrapper]()

// dymMatchInputContext implements shared.MatchInputContext.
type dymMatchInputContext struct {
	hostInputPtr C.envoy_dynamic_module_type_matcher_input_envoy_ptr
}

func (c *dymMatchInputContext) GetHeadersSize(headerType shared.HttpHeaderType) uint64 {
	return uint64(C.envoy_dynamic_module_callback_matcher_get_headers_size(
		c.hostInputPtr, C.envoy_dynamic_module_type_http_header_type(headerType)))
}

func (c *dymMatchInputContext) GetHeaders(headerType shared.HttpHeaderType) [][2]shared.UnsafeEnvoyBuffer {
	size := C.envoy_dynamic_module_callback_matcher_get_headers_size(
		c.hostInputPtr, C.envoy_dynamic_module_type_http_header_type(headerType))
	if size == 0 {
		return nil
	}
	hdrs := make([]C.envoy_dynamic_module_type_envoy_http_header, int(size))
	if !bool(C.envoy_dynamic_module_callback_matcher_get_headers(
		c.hostInputPtr, C.envoy_dynamic_module_type_http_header_type(headerType),
		unsafe.SliceData(hdrs))) {
		return nil
	}
	out := envoyHttpHeaderSliceToUnsafeHeaderSlice(hdrs)
	runtime.KeepAlive(hdrs)
	return out
}

func (c *dymMatchInputContext) GetHeaderValue(headerType shared.HttpHeaderType, key string, index uint64) (shared.UnsafeEnvoyBuffer, uint64, bool) {
	var buf C.envoy_dynamic_module_type_envoy_buffer
	var total C.size_t
	ret := C.envoy_dynamic_module_callback_matcher_get_header_value(
		c.hostInputPtr,
		C.envoy_dynamic_module_type_http_header_type(headerType),
		stringToModuleBuffer(key),
		&buf,
		C.size_t(index),
		&total,
	)
	runtime.KeepAlive(key)
	if !bool(ret) {
		return shared.UnsafeEnvoyBuffer{}, uint64(total), false
	}
	if buf.ptr == nil || buf.length == 0 {
		return shared.UnsafeEnvoyBuffer{}, uint64(total), true
	}
	return envoyBufferToUnsafeEnvoyBuffer(buf), uint64(total), true
}

// =============================================================================
// Event hooks
// =============================================================================

//export envoy_dynamic_module_on_matcher_config_new
func envoy_dynamic_module_on_matcher_config_new(
	_ C.envoy_dynamic_module_type_matcher_config_envoy_ptr,
	name C.envoy_dynamic_module_type_envoy_buffer,
	config C.envoy_dynamic_module_type_envoy_buffer,
) C.envoy_dynamic_module_type_matcher_config_module_ptr {
	nameStr := envoyBufferToStringUnsafe(name)
	configBytes := envoyBufferToBytesCopy(config)

	configFactory := sdk.GetMatcherConfigFactory(nameStr)
	if configFactory == nil {
		hostLog(shared.LogLevelWarn, "Failed to load matcher configuration: no factory for %s", []any{nameStr})
		return nil
	}
	matcher, err := configFactory.Create(nameStr, configBytes)
	if err != nil || matcher == nil {
		hostLog(shared.LogLevelWarn, "Failed to load matcher configuration: %v", []any{err})
		return nil
	}
	wrapper := &matcherConfigWrapper{matcher: matcher}
	configPtr := matcherConfigManager.record(wrapper)
	return C.envoy_dynamic_module_type_matcher_config_module_ptr(configPtr)
}

//export envoy_dynamic_module_on_matcher_config_destroy
func envoy_dynamic_module_on_matcher_config_destroy(
	configPtr C.envoy_dynamic_module_type_matcher_config_module_ptr,
) {
	w := matcherConfigManager.unwrap(unsafe.Pointer(configPtr))
	if w == nil {
		return
	}
	if w.matcher != nil {
		w.matcher.OnDestroy()
	}
	matcherConfigManager.remove(unsafe.Pointer(configPtr))
}

//export envoy_dynamic_module_on_matcher_match
func envoy_dynamic_module_on_matcher_match(
	configPtr C.envoy_dynamic_module_type_matcher_config_module_ptr,
	inputPtr C.envoy_dynamic_module_type_matcher_input_envoy_ptr,
) C.bool {
	w := matcherConfigManager.unwrap(unsafe.Pointer(configPtr))
	if w == nil || w.matcher == nil {
		return false
	}
	ctx := &dymMatchInputContext{hostInputPtr: inputPtr}
	return C.bool(w.matcher.OnMatch(ctx))
}
