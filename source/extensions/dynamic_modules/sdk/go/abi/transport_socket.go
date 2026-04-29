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

type tsFactoryConfigWrapper struct {
	factory shared.TransportSocketFactory
}

type tsSocketWrapper struct {
	socket shared.TransportSocket

	// Memory caches that keep Go slices alive while their pointers cross the ABI for the
	// duration of GetProtocol / GetFailureReason callbacks.
	protocolCache []byte
	failureCache  []byte
}

var tsFactoryConfigManager = newManager[tsFactoryConfigWrapper]()
var tsSocketManager = newManager[tsSocketWrapper]()

// dymTransportSocketHandle implements shared.TransportSocketHandle.
type dymTransportSocketHandle struct {
	hostSocketPtr C.envoy_dynamic_module_type_transport_socket_envoy_ptr
}

func (h *dymTransportSocketHandle) IoHandleRead(buffer []byte) (int64, int64) {
	if len(buffer) == 0 {
		return 0, 0
	}
	io := C.envoy_dynamic_module_callback_transport_socket_get_io_handle(h.hostSocketPtr)
	if io == nil {
		return 0, -1
	}
	var bytesRead C.size_t
	rc := C.envoy_dynamic_module_callback_transport_socket_io_handle_read(
		io,
		(*C.char)(unsafe.Pointer(unsafe.SliceData(buffer))),
		C.size_t(len(buffer)),
		&bytesRead,
	)
	runtime.KeepAlive(buffer)
	return int64(bytesRead), int64(rc)
}

func (h *dymTransportSocketHandle) IoHandleWrite(buffer []byte) (int64, int64) {
	if len(buffer) == 0 {
		return 0, 0
	}
	io := C.envoy_dynamic_module_callback_transport_socket_get_io_handle(h.hostSocketPtr)
	if io == nil {
		return 0, -1
	}
	var bytesWritten C.size_t
	rc := C.envoy_dynamic_module_callback_transport_socket_io_handle_write(
		io,
		(*C.char)(unsafe.Pointer(unsafe.SliceData(buffer))),
		C.size_t(len(buffer)),
		&bytesWritten,
	)
	runtime.KeepAlive(buffer)
	return int64(bytesWritten), int64(rc)
}

func (h *dymTransportSocketHandle) IoHandleFD() int32 {
	io := C.envoy_dynamic_module_callback_transport_socket_get_io_handle(h.hostSocketPtr)
	if io == nil {
		return -1
	}
	return int32(C.envoy_dynamic_module_callback_transport_socket_io_handle_fd(io))
}

func (h *dymTransportSocketHandle) DrainReadBuffer(length uint64) {
	C.envoy_dynamic_module_callback_transport_socket_read_buffer_drain(h.hostSocketPtr, C.size_t(length))
}

func (h *dymTransportSocketHandle) AddToReadBuffer(data []byte) {
	if len(data) == 0 {
		return
	}
	C.envoy_dynamic_module_callback_transport_socket_read_buffer_add(
		h.hostSocketPtr,
		(*C.char)(unsafe.Pointer(unsafe.SliceData(data))),
		C.size_t(len(data)),
	)
	runtime.KeepAlive(data)
}

func (h *dymTransportSocketHandle) ReadBufferLength() uint64 {
	return uint64(C.envoy_dynamic_module_callback_transport_socket_read_buffer_length(h.hostSocketPtr))
}

func (h *dymTransportSocketHandle) DrainWriteBuffer(length uint64) {
	C.envoy_dynamic_module_callback_transport_socket_write_buffer_drain(h.hostSocketPtr, C.size_t(length))
}

func (h *dymTransportSocketHandle) GetWriteBufferSlices() []shared.UnsafeEnvoyBuffer {
	// Query mode: pass slices=NULL to get the count.
	var count C.size_t
	C.envoy_dynamic_module_callback_transport_socket_write_buffer_get_slices(h.hostSocketPtr, nil, &count)
	if count == 0 {
		return nil
	}
	bufs := make([]C.envoy_dynamic_module_type_envoy_buffer, int(count))
	C.envoy_dynamic_module_callback_transport_socket_write_buffer_get_slices(
		h.hostSocketPtr, unsafe.SliceData(bufs), &count)
	out := envoyBufferSliceToUnsafeEnvoyBufferSlice(bufs[:int(count)])
	runtime.KeepAlive(bufs)
	return out
}

func (h *dymTransportSocketHandle) WriteBufferLength() uint64 {
	return uint64(C.envoy_dynamic_module_callback_transport_socket_write_buffer_length(h.hostSocketPtr))
}

func (h *dymTransportSocketHandle) RaiseEvent(event shared.NetworkConnectionEvent) {
	C.envoy_dynamic_module_callback_transport_socket_raise_event(
		h.hostSocketPtr, C.envoy_dynamic_module_type_network_connection_event(event))
}

func (h *dymTransportSocketHandle) ShouldDrainReadBuffer() bool {
	return bool(C.envoy_dynamic_module_callback_transport_socket_should_drain_read_buffer(h.hostSocketPtr))
}

func (h *dymTransportSocketHandle) SetIsReadable() {
	C.envoy_dynamic_module_callback_transport_socket_set_is_readable(h.hostSocketPtr)
}

func (h *dymTransportSocketHandle) FlushWriteBuffer() {
	C.envoy_dynamic_module_callback_transport_socket_flush_write_buffer(h.hostSocketPtr)
}

// =============================================================================
// Event hooks
// =============================================================================

//export envoy_dynamic_module_on_transport_socket_factory_config_new
func envoy_dynamic_module_on_transport_socket_factory_config_new(
	_ C.envoy_dynamic_module_type_transport_socket_factory_config_envoy_ptr,
	socketName C.envoy_dynamic_module_type_envoy_buffer,
	socketConfig C.envoy_dynamic_module_type_envoy_buffer,
	isUpstream C.bool,
) C.envoy_dynamic_module_type_transport_socket_factory_config_module_ptr {
	nameStr := envoyBufferToStringUnsafe(socketName)
	configBytes := envoyBufferToBytesUnsafe(socketConfig)

	configFactory := sdk.GetTransportSocketFactoryConfigFactory(nameStr)
	if configFactory == nil {
		hostLog(shared.LogLevelWarn, "Failed to load transport socket configuration: no factory for %s", []any{nameStr})
		return nil
	}
	factory, err := configFactory.Create(nameStr, configBytes, bool(isUpstream))
	if err != nil || factory == nil {
		hostLog(shared.LogLevelWarn, "Failed to load transport socket configuration: %v", []any{err})
		return nil
	}
	wrapper := &tsFactoryConfigWrapper{factory: factory}
	configPtr := tsFactoryConfigManager.record(wrapper)
	return C.envoy_dynamic_module_type_transport_socket_factory_config_module_ptr(configPtr)
}

//export envoy_dynamic_module_on_transport_socket_factory_config_destroy
func envoy_dynamic_module_on_transport_socket_factory_config_destroy(
	configPtr C.envoy_dynamic_module_type_transport_socket_factory_config_module_ptr,
) {
	w := tsFactoryConfigManager.unwrap(unsafe.Pointer(configPtr))
	if w == nil {
		return
	}
	w.factory.OnDestroy()
	tsFactoryConfigManager.remove(unsafe.Pointer(configPtr))
}

//export envoy_dynamic_module_on_transport_socket_new
func envoy_dynamic_module_on_transport_socket_new(
	configPtr C.envoy_dynamic_module_type_transport_socket_factory_config_module_ptr,
	hostSocketPtr C.envoy_dynamic_module_type_transport_socket_envoy_ptr,
) C.envoy_dynamic_module_type_transport_socket_module_ptr {
	cfg := tsFactoryConfigManager.unwrap(unsafe.Pointer(configPtr))
	if cfg == nil {
		return nil
	}
	handle := &dymTransportSocketHandle{hostSocketPtr: hostSocketPtr}
	socket := cfg.factory.Create(handle)
	if socket == nil {
		return nil
	}
	wrapper := &tsSocketWrapper{socket: socket}
	socketPtr := tsSocketManager.record(wrapper)
	return C.envoy_dynamic_module_type_transport_socket_module_ptr(socketPtr)
}

//export envoy_dynamic_module_on_transport_socket_destroy
func envoy_dynamic_module_on_transport_socket_destroy(
	socketPtr C.envoy_dynamic_module_type_transport_socket_module_ptr,
) {
	w := tsSocketManager.unwrap(unsafe.Pointer(socketPtr))
	if w == nil {
		return
	}
	if w.socket != nil {
		w.socket.OnDestroy()
	}
	tsSocketManager.remove(unsafe.Pointer(socketPtr))
}

//export envoy_dynamic_module_on_transport_socket_set_callbacks
func envoy_dynamic_module_on_transport_socket_set_callbacks(
	hostSocketPtr C.envoy_dynamic_module_type_transport_socket_envoy_ptr,
	socketPtr C.envoy_dynamic_module_type_transport_socket_module_ptr,
) {
	w := tsSocketManager.unwrap(unsafe.Pointer(socketPtr))
	if w == nil || w.socket == nil {
		return
	}
	handle := &dymTransportSocketHandle{hostSocketPtr: hostSocketPtr}
	w.socket.SetCallbacks(handle)
}

//export envoy_dynamic_module_on_transport_socket_on_connected
func envoy_dynamic_module_on_transport_socket_on_connected(
	hostSocketPtr C.envoy_dynamic_module_type_transport_socket_envoy_ptr,
	socketPtr C.envoy_dynamic_module_type_transport_socket_module_ptr,
) {
	w := tsSocketManager.unwrap(unsafe.Pointer(socketPtr))
	if w == nil || w.socket == nil {
		return
	}
	handle := &dymTransportSocketHandle{hostSocketPtr: hostSocketPtr}
	w.socket.OnConnected(handle)
}

//export envoy_dynamic_module_on_transport_socket_do_read
func envoy_dynamic_module_on_transport_socket_do_read(
	hostSocketPtr C.envoy_dynamic_module_type_transport_socket_envoy_ptr,
	socketPtr C.envoy_dynamic_module_type_transport_socket_module_ptr,
) C.envoy_dynamic_module_type_transport_socket_io_result {
	w := tsSocketManager.unwrap(unsafe.Pointer(socketPtr))
	if w == nil || w.socket == nil {
		return C.envoy_dynamic_module_type_transport_socket_io_result{
			action: C.envoy_dynamic_module_type_transport_socket_post_io_action_Close,
		}
	}
	handle := &dymTransportSocketHandle{hostSocketPtr: hostSocketPtr}
	r := w.socket.DoRead(handle)
	return C.envoy_dynamic_module_type_transport_socket_io_result{
		action:          C.envoy_dynamic_module_type_transport_socket_post_io_action(r.Action),
		bytes_processed: C.uint64_t(r.BytesProcessed),
		end_stream_read: C.bool(r.EndStreamRead),
	}
}

//export envoy_dynamic_module_on_transport_socket_do_write
func envoy_dynamic_module_on_transport_socket_do_write(
	hostSocketPtr C.envoy_dynamic_module_type_transport_socket_envoy_ptr,
	socketPtr C.envoy_dynamic_module_type_transport_socket_module_ptr,
	writeBufferLength C.size_t,
	endStream C.bool,
) C.envoy_dynamic_module_type_transport_socket_io_result {
	w := tsSocketManager.unwrap(unsafe.Pointer(socketPtr))
	if w == nil || w.socket == nil {
		return C.envoy_dynamic_module_type_transport_socket_io_result{
			action: C.envoy_dynamic_module_type_transport_socket_post_io_action_Close,
		}
	}
	handle := &dymTransportSocketHandle{hostSocketPtr: hostSocketPtr}
	r := w.socket.DoWrite(handle, uint64(writeBufferLength), bool(endStream))
	return C.envoy_dynamic_module_type_transport_socket_io_result{
		action:          C.envoy_dynamic_module_type_transport_socket_post_io_action(r.Action),
		bytes_processed: C.uint64_t(r.BytesProcessed),
		end_stream_read: C.bool(r.EndStreamRead),
	}
}

//export envoy_dynamic_module_on_transport_socket_close
func envoy_dynamic_module_on_transport_socket_close(
	hostSocketPtr C.envoy_dynamic_module_type_transport_socket_envoy_ptr,
	socketPtr C.envoy_dynamic_module_type_transport_socket_module_ptr,
	event C.envoy_dynamic_module_type_network_connection_event,
) {
	w := tsSocketManager.unwrap(unsafe.Pointer(socketPtr))
	if w == nil || w.socket == nil {
		return
	}
	handle := &dymTransportSocketHandle{hostSocketPtr: hostSocketPtr}
	w.socket.OnClose(handle, shared.NetworkConnectionEvent(event))
}

//export envoy_dynamic_module_on_transport_socket_get_protocol
func envoy_dynamic_module_on_transport_socket_get_protocol(
	_ C.envoy_dynamic_module_type_transport_socket_envoy_ptr,
	socketPtr C.envoy_dynamic_module_type_transport_socket_module_ptr,
	result *C.envoy_dynamic_module_type_module_buffer,
) {
	w := tsSocketManager.unwrap(unsafe.Pointer(socketPtr))
	if w == nil || w.socket == nil {
		result.ptr = nil
		result.length = 0
		return
	}
	w.protocolCache = w.socket.GetProtocol()
	if len(w.protocolCache) == 0 {
		result.ptr = nil
		result.length = 0
		return
	}
	result.ptr = (*C.char)(unsafe.Pointer(unsafe.SliceData(w.protocolCache)))
	result.length = C.size_t(len(w.protocolCache))
}

//export envoy_dynamic_module_on_transport_socket_get_failure_reason
func envoy_dynamic_module_on_transport_socket_get_failure_reason(
	_ C.envoy_dynamic_module_type_transport_socket_envoy_ptr,
	socketPtr C.envoy_dynamic_module_type_transport_socket_module_ptr,
	result *C.envoy_dynamic_module_type_module_buffer,
) {
	w := tsSocketManager.unwrap(unsafe.Pointer(socketPtr))
	if w == nil || w.socket == nil {
		result.ptr = nil
		result.length = 0
		return
	}
	w.failureCache = w.socket.GetFailureReason()
	if len(w.failureCache) == 0 {
		result.ptr = nil
		result.length = 0
		return
	}
	result.ptr = (*C.char)(unsafe.Pointer(unsafe.SliceData(w.failureCache)))
	result.length = C.size_t(len(w.failureCache))
}

//export envoy_dynamic_module_on_transport_socket_can_flush_close
func envoy_dynamic_module_on_transport_socket_can_flush_close(
	_ C.envoy_dynamic_module_type_transport_socket_envoy_ptr,
	socketPtr C.envoy_dynamic_module_type_transport_socket_module_ptr,
) C.bool {
	w := tsSocketManager.unwrap(unsafe.Pointer(socketPtr))
	if w == nil || w.socket == nil {
		return true
	}
	return C.bool(w.socket.CanFlushClose())
}
