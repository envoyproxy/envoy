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

type uhtbConfigWrapper struct {
	factory shared.UpstreamHttpTcpBridgeFactory
}

type uhtbBridgeWrapper = dymUpstreamHttpTcpBridgeHandle

var uhtbConfigManager = newManager[uhtbConfigWrapper]()
var uhtbBridgeManager = newManager[uhtbBridgeWrapper]()

// dymUpstreamHttpTcpBridgeHandle implements shared.UpstreamHttpTcpBridgeHandle.
type dymUpstreamHttpTcpBridgeHandle struct {
	hostBridgePtr C.envoy_dynamic_module_type_upstream_http_tcp_bridge_envoy_ptr

	bridge    shared.UpstreamHttpTcpBridge
	destroyed bool
}

func (h *dymUpstreamHttpTcpBridgeHandle) GetRequestHeader(key string, index uint64) (shared.UnsafeEnvoyBuffer, uint64, bool) {
	var buf C.envoy_dynamic_module_type_envoy_buffer
	var total C.size_t
	ret := C.envoy_dynamic_module_callback_upstream_http_tcp_bridge_get_request_header(
		h.hostBridgePtr, stringToModuleBuffer(key), &buf, C.size_t(index), &total,
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

func (h *dymUpstreamHttpTcpBridgeHandle) GetRequestHeadersSize() uint64 {
	return uint64(C.envoy_dynamic_module_callback_upstream_http_tcp_bridge_get_request_headers_size(h.hostBridgePtr))
}

func (h *dymUpstreamHttpTcpBridgeHandle) GetRequestHeaders() [][2]shared.UnsafeEnvoyBuffer {
	size := C.envoy_dynamic_module_callback_upstream_http_tcp_bridge_get_request_headers_size(h.hostBridgePtr)
	if size == 0 {
		return nil
	}
	hdrs := make([]C.envoy_dynamic_module_type_envoy_http_header, int(size))
	if !bool(C.envoy_dynamic_module_callback_upstream_http_tcp_bridge_get_request_headers(
		h.hostBridgePtr, unsafe.SliceData(hdrs))) {
		return nil
	}
	out := envoyHttpHeaderSliceToUnsafeHeaderSlice(hdrs)
	runtime.KeepAlive(hdrs)
	return out
}

func (h *dymUpstreamHttpTcpBridgeHandle) getBufferChunks(
	getFn func(C.envoy_dynamic_module_type_upstream_http_tcp_bridge_envoy_ptr, *C.envoy_dynamic_module_type_envoy_buffer, *C.size_t),
) []shared.UnsafeEnvoyBuffer {
	// The ABI here returns chunks via an output buffer pointer + count. We call once to learn
	// the count by passing a null buffer — Envoy fills only the count when buffer is null.
	// Then allocate and call again. To avoid a second call, allocate a sized array based on
	// a worst-case heuristic (16 chunks) and grow if needed.
	buf := make([]C.envoy_dynamic_module_type_envoy_buffer, 16)
	var count C.size_t = C.size_t(len(buf))
	getFn(h.hostBridgePtr, unsafe.SliceData(buf), &count)
	if int(count) > len(buf) {
		buf = make([]C.envoy_dynamic_module_type_envoy_buffer, int(count))
		count = C.size_t(len(buf))
		getFn(h.hostBridgePtr, unsafe.SliceData(buf), &count)
	}
	if count == 0 {
		return nil
	}
	out := envoyBufferSliceToUnsafeEnvoyBufferSlice(buf[:int(count)])
	runtime.KeepAlive(buf)
	return out
}

func (h *dymUpstreamHttpTcpBridgeHandle) GetRequestBuffer() []shared.UnsafeEnvoyBuffer {
	return h.getBufferChunks(func(p C.envoy_dynamic_module_type_upstream_http_tcp_bridge_envoy_ptr, buf *C.envoy_dynamic_module_type_envoy_buffer, n *C.size_t) {
		C.envoy_dynamic_module_callback_upstream_http_tcp_bridge_get_request_buffer(p, buf, n)
	})
}

func (h *dymUpstreamHttpTcpBridgeHandle) GetResponseBuffer() []shared.UnsafeEnvoyBuffer {
	return h.getBufferChunks(func(p C.envoy_dynamic_module_type_upstream_http_tcp_bridge_envoy_ptr, buf *C.envoy_dynamic_module_type_envoy_buffer, n *C.size_t) {
		C.envoy_dynamic_module_callback_upstream_http_tcp_bridge_get_response_buffer(p, buf, n)
	})
}

func (h *dymUpstreamHttpTcpBridgeHandle) SendUpstreamData(data []byte, endOfStream bool) {
	C.envoy_dynamic_module_callback_upstream_http_tcp_bridge_send_upstream_data(
		h.hostBridgePtr, bytesToModuleBuffer(data), C.bool(endOfStream))
	runtime.KeepAlive(data)
}

func (h *dymUpstreamHttpTcpBridgeHandle) SendResponse(statusCode uint32, headers [][2]string, body []byte) {
	headerViews := headersToModuleHttpHeaderSlice(headers)
	var headerPtr *C.envoy_dynamic_module_type_module_http_header
	if len(headerViews) > 0 {
		headerPtr = unsafe.SliceData(headerViews)
	}
	C.envoy_dynamic_module_callback_upstream_http_tcp_bridge_send_response(
		h.hostBridgePtr,
		C.uint32_t(statusCode),
		headerPtr,
		C.size_t(len(headerViews)),
		bytesToModuleBuffer(body),
	)
	runtime.KeepAlive(headers)
	runtime.KeepAlive(headerViews)
	runtime.KeepAlive(body)
}

func (h *dymUpstreamHttpTcpBridgeHandle) SendResponseHeaders(statusCode uint32, headers [][2]string, endOfStream bool) {
	headerViews := headersToModuleHttpHeaderSlice(headers)
	var headerPtr *C.envoy_dynamic_module_type_module_http_header
	if len(headerViews) > 0 {
		headerPtr = unsafe.SliceData(headerViews)
	}
	C.envoy_dynamic_module_callback_upstream_http_tcp_bridge_send_response_headers(
		h.hostBridgePtr,
		C.uint32_t(statusCode),
		headerPtr,
		C.size_t(len(headerViews)),
		C.bool(endOfStream),
	)
	runtime.KeepAlive(headers)
	runtime.KeepAlive(headerViews)
}

func (h *dymUpstreamHttpTcpBridgeHandle) SendResponseData(data []byte, endOfStream bool) {
	C.envoy_dynamic_module_callback_upstream_http_tcp_bridge_send_response_data(
		h.hostBridgePtr, bytesToModuleBuffer(data), C.bool(endOfStream))
	runtime.KeepAlive(data)
}

func (h *dymUpstreamHttpTcpBridgeHandle) SendResponseTrailers(trailers [][2]string) {
	trailerViews := headersToModuleHttpHeaderSlice(trailers)
	var trailerPtr *C.envoy_dynamic_module_type_module_http_header
	if len(trailerViews) > 0 {
		trailerPtr = unsafe.SliceData(trailerViews)
	}
	C.envoy_dynamic_module_callback_upstream_http_tcp_bridge_send_response_trailers(
		h.hostBridgePtr, trailerPtr, C.size_t(len(trailerViews)))
	runtime.KeepAlive(trailers)
	runtime.KeepAlive(trailerViews)
}

// =============================================================================
// Event hooks
// =============================================================================

//export envoy_dynamic_module_on_upstream_http_tcp_bridge_config_new
func envoy_dynamic_module_on_upstream_http_tcp_bridge_config_new(
	_ C.envoy_dynamic_module_type_upstream_http_tcp_bridge_config_envoy_ptr,
	name C.envoy_dynamic_module_type_envoy_buffer,
	config C.envoy_dynamic_module_type_envoy_buffer,
) C.envoy_dynamic_module_type_upstream_http_tcp_bridge_config_module_ptr {
	nameStr := envoyBufferToStringUnsafe(name)
	configBytes := envoyBufferToBytesCopy(config)

	configFactory := sdk.GetUpstreamHttpTcpBridgeConfigFactory(nameStr)
	if configFactory == nil {
		hostLog(shared.LogLevelWarn, "Failed to load upstream HTTP/TCP bridge configuration: no factory for %s", []any{nameStr})
		return nil
	}
	factory, err := configFactory.Create(nameStr, configBytes)
	if err != nil || factory == nil {
		hostLog(shared.LogLevelWarn, "Failed to load upstream HTTP/TCP bridge configuration: %v", []any{err})
		return nil
	}
	wrapper := &uhtbConfigWrapper{factory: factory}
	configPtr := uhtbConfigManager.record(wrapper)
	return C.envoy_dynamic_module_type_upstream_http_tcp_bridge_config_module_ptr(configPtr)
}

//export envoy_dynamic_module_on_upstream_http_tcp_bridge_config_destroy
func envoy_dynamic_module_on_upstream_http_tcp_bridge_config_destroy(
	configPtr C.envoy_dynamic_module_type_upstream_http_tcp_bridge_config_module_ptr,
) {
	w := uhtbConfigManager.unwrap(unsafe.Pointer(configPtr))
	if w == nil {
		return
	}
	w.factory.OnDestroy()
	uhtbConfigManager.remove(unsafe.Pointer(configPtr))
}

//export envoy_dynamic_module_on_upstream_http_tcp_bridge_new
func envoy_dynamic_module_on_upstream_http_tcp_bridge_new(
	configPtr C.envoy_dynamic_module_type_upstream_http_tcp_bridge_config_module_ptr,
	hostBridgePtr C.envoy_dynamic_module_type_upstream_http_tcp_bridge_envoy_ptr,
) C.envoy_dynamic_module_type_upstream_http_tcp_bridge_module_ptr {
	cfg := uhtbConfigManager.unwrap(unsafe.Pointer(configPtr))
	if cfg == nil {
		return nil
	}
	handle := &dymUpstreamHttpTcpBridgeHandle{hostBridgePtr: hostBridgePtr}
	handle.bridge = cfg.factory.Create(handle)
	if handle.bridge == nil {
		return nil
	}
	bridgePtr := uhtbBridgeManager.record(handle)
	return C.envoy_dynamic_module_type_upstream_http_tcp_bridge_module_ptr(bridgePtr)
}

//export envoy_dynamic_module_on_upstream_http_tcp_bridge_encode_headers
func envoy_dynamic_module_on_upstream_http_tcp_bridge_encode_headers(
	_ C.envoy_dynamic_module_type_upstream_http_tcp_bridge_envoy_ptr,
	bridgePtr C.envoy_dynamic_module_type_upstream_http_tcp_bridge_module_ptr,
	endOfStream C.bool,
) {
	h := uhtbBridgeManager.unwrap(unsafe.Pointer(bridgePtr))
	if h == nil || h.bridge == nil || h.destroyed {
		return
	}
	h.bridge.EncodeHeaders(h, bool(endOfStream))
}

//export envoy_dynamic_module_on_upstream_http_tcp_bridge_encode_data
func envoy_dynamic_module_on_upstream_http_tcp_bridge_encode_data(
	_ C.envoy_dynamic_module_type_upstream_http_tcp_bridge_envoy_ptr,
	bridgePtr C.envoy_dynamic_module_type_upstream_http_tcp_bridge_module_ptr,
	endOfStream C.bool,
) {
	h := uhtbBridgeManager.unwrap(unsafe.Pointer(bridgePtr))
	if h == nil || h.bridge == nil || h.destroyed {
		return
	}
	h.bridge.EncodeData(h, bool(endOfStream))
}

//export envoy_dynamic_module_on_upstream_http_tcp_bridge_encode_trailers
func envoy_dynamic_module_on_upstream_http_tcp_bridge_encode_trailers(
	_ C.envoy_dynamic_module_type_upstream_http_tcp_bridge_envoy_ptr,
	bridgePtr C.envoy_dynamic_module_type_upstream_http_tcp_bridge_module_ptr,
) {
	h := uhtbBridgeManager.unwrap(unsafe.Pointer(bridgePtr))
	if h == nil || h.bridge == nil || h.destroyed {
		return
	}
	h.bridge.EncodeTrailers(h)
}

//export envoy_dynamic_module_on_upstream_http_tcp_bridge_on_upstream_data
func envoy_dynamic_module_on_upstream_http_tcp_bridge_on_upstream_data(
	_ C.envoy_dynamic_module_type_upstream_http_tcp_bridge_envoy_ptr,
	bridgePtr C.envoy_dynamic_module_type_upstream_http_tcp_bridge_module_ptr,
	endOfStream C.bool,
) {
	h := uhtbBridgeManager.unwrap(unsafe.Pointer(bridgePtr))
	if h == nil || h.bridge == nil || h.destroyed {
		return
	}
	h.bridge.OnUpstreamData(h, bool(endOfStream))
}

//export envoy_dynamic_module_on_upstream_http_tcp_bridge_destroy
func envoy_dynamic_module_on_upstream_http_tcp_bridge_destroy(
	bridgePtr C.envoy_dynamic_module_type_upstream_http_tcp_bridge_module_ptr,
) {
	h := uhtbBridgeManager.unwrap(unsafe.Pointer(bridgePtr))
	if h == nil || h.destroyed {
		return
	}
	h.destroyed = true
	if h.bridge != nil {
		h.bridge.OnDestroy()
	}
	uhtbBridgeManager.remove(unsafe.Pointer(bridgePtr))
}
