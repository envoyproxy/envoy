package abi

/*
#include <stdint.h>
#include <stdlib.h>
#include "../../../abi/abi.h"

static inline void bootstrap_complete(envoy_dynamic_module_type_event_cb callback, void* context) {
  callback(context);
}
*/
import "C"

import (
	"runtime/cgo"
	"sync"
	"unsafe"

	sdk "github.com/envoyproxy/envoy/source/extensions/dynamic_modules/sdk/go"
	"github.com/envoyproxy/envoy/source/extensions/dynamic_modules/sdk/go/internal/recovery"
	"github.com/envoyproxy/envoy/source/extensions/dynamic_modules/sdk/go/shared"
)

// Envoy retains C-allocated boxes containing cgo handles, rather than Go pointers.
// Configurations and extension instances have independent ownership and destruction.
func newBootstrapHandle(value any) unsafe.Pointer {
	box := C.malloc(C.size_t(unsafe.Sizeof(C.uintptr_t(0))))
	if box == nil {
		panic("allocating bootstrap handle")
	}
	*(*C.uintptr_t)(box) = C.uintptr_t(cgo.NewHandle(value))
	return box
}

func bootstrapValue[T any](box unsafe.Pointer) T {
	return cgo.Handle(*(*C.uintptr_t)(box)).Value().(T)
}

func deleteBootstrapHandle(box unsafe.Pointer) {
	cgo.Handle(*(*C.uintptr_t)(box)).Delete()
	C.free(box)
}

type bootstrapConfigWrapper struct {
	config shared.BootstrapExtensionConfig
	handle *dymBootstrapConfigHandle
}

type dymBootstrapConfigHandle struct {
	dymCommonHandle
	host      C.envoy_dynamic_module_type_bootstrap_extension_config_envoy_ptr
	scheduler *bootstrapScheduler
}

func (h *dymBootstrapConfigHandle) Log(level shared.LogLevel, format string, args ...any) {
	hostLog(level, format, args)
}

func (h *dymBootstrapConfigHandle) SignalInitComplete() {
	C.envoy_dynamic_module_callback_bootstrap_extension_config_signal_init_complete(h.host)
}

func (h *dymBootstrapConfigHandle) GetScheduler() shared.Scheduler {
	if h.scheduler == nil {
		ptr := C.envoy_dynamic_module_callback_bootstrap_extension_config_scheduler_new(h.host)
		h.scheduler = &bootstrapScheduler{scheduler: newDymScheduler(unsafe.Pointer(ptr),
			func(ptr unsafe.Pointer, id C.uint64_t) {
				C.envoy_dynamic_module_callback_bootstrap_extension_config_scheduler_commit(
					C.envoy_dynamic_module_type_bootstrap_extension_config_scheduler_module_ptr(ptr), id)
			})}
	}
	return h.scheduler
}

func (h *dymBootstrapConfigHandle) close() {
	if h.scheduler != nil {
		h.scheduler.close()
	}
}

// A native commit can release the last reference to the configuration and re-enter
// its destroy hook. Keep the scheduler alive across commits without holding a lock
// across the ABI call or waiting for those commits during destruction.
type bootstrapScheduler struct {
	mu        sync.Mutex
	scheduler *dymScheduler
	closed    bool
	active    uint64
}

func (s *bootstrapScheduler) Schedule(task func()) {
	s.mu.Lock()
	if s.closed {
		s.mu.Unlock()
		return
	}
	s.active++
	s.mu.Unlock()
	defer func() {
		s.mu.Lock()
		defer s.mu.Unlock()
		s.active--
		if s.closed && s.active == 0 {
			s.destroy()
		}
	}()
	s.scheduler.Schedule(task)
}

func (s *bootstrapScheduler) close() {
	s.mu.Lock()
	defer s.mu.Unlock()
	if s.closed {
		return
	}
	s.closed = true
	if s.active == 0 {
		s.destroy()
	}
}

// destroy runs under mu after the last commit returns.
func (s *bootstrapScheduler) destroy() {
	C.envoy_dynamic_module_callback_bootstrap_extension_config_scheduler_delete(
		C.envoy_dynamic_module_type_bootstrap_extension_config_scheduler_module_ptr(s.scheduler.schedulerPtr))
	s.scheduler.schedulerLock.Lock()
	clear(s.scheduler.tasks)
	s.scheduler.schedulerLock.Unlock()
}

type dymBootstrapExtensionHandle struct{}

func (dymBootstrapExtensionHandle) Log(level shared.LogLevel, format string, args ...any) {
	hostLog(level, format, args)
}

//export envoy_dynamic_module_on_bootstrap_extension_config_new
func envoy_dynamic_module_on_bootstrap_extension_config_new(
	host C.envoy_dynamic_module_type_bootstrap_extension_config_envoy_ptr,
	name, config C.envoy_dynamic_module_type_envoy_buffer,
) (result C.envoy_dynamic_module_type_bootstrap_extension_config_module_ptr) {
	defer recovery.Export("envoy_dynamic_module_on_bootstrap_extension_config_new", nil, &result)
	handle := &dymBootstrapConfigHandle{host: host}
	retained := false
	defer func() {
		if !retained {
			handle.close()
		}
	}()
	extensionConfig, err := sdk.NewBootstrapExtensionConfig(handle,
		envoyBufferToStringUnsafe(name), envoyBufferToBytesUnsafe(config))
	if err != nil || extensionConfig == nil {
		handle.Log(shared.LogLevelWarn, "Failed to load bootstrap configuration: %v", err)
		return nil
	}
	result = C.envoy_dynamic_module_type_bootstrap_extension_config_module_ptr(
		newBootstrapHandle(&bootstrapConfigWrapper{config: extensionConfig, handle: handle}))
	retained = true
	return result
}

//export envoy_dynamic_module_on_bootstrap_extension_config_destroy
func envoy_dynamic_module_on_bootstrap_extension_config_destroy(ptr C.envoy_dynamic_module_type_bootstrap_extension_config_module_ptr) {
	defer recovery.ExportVoid("envoy_dynamic_module_on_bootstrap_extension_config_destroy")
	box := unsafe.Pointer(ptr)
	wrapper := bootstrapValue[*bootstrapConfigWrapper](box)
	defer deleteBootstrapHandle(box)
	defer wrapper.handle.close()
	wrapper.config.OnDestroy()
}

//export envoy_dynamic_module_on_bootstrap_extension_new
func envoy_dynamic_module_on_bootstrap_extension_new(
	ptr C.envoy_dynamic_module_type_bootstrap_extension_config_module_ptr,
	_ C.envoy_dynamic_module_type_bootstrap_extension_envoy_ptr,
) (result C.envoy_dynamic_module_type_bootstrap_extension_module_ptr) {
	defer recovery.Export("envoy_dynamic_module_on_bootstrap_extension_new", nil, &result)
	wrapper := bootstrapValue[*bootstrapConfigWrapper](unsafe.Pointer(ptr))
	extension := wrapper.config.NewBootstrapExtension(dymBootstrapExtensionHandle{})
	if extension == nil {
		return nil
	}
	return C.envoy_dynamic_module_type_bootstrap_extension_module_ptr(newBootstrapHandle(extension))
}

//export envoy_dynamic_module_on_bootstrap_extension_destroy
func envoy_dynamic_module_on_bootstrap_extension_destroy(ptr C.envoy_dynamic_module_type_bootstrap_extension_module_ptr) {
	defer recovery.ExportVoid("envoy_dynamic_module_on_bootstrap_extension_destroy")
	box := unsafe.Pointer(ptr)
	defer deleteBootstrapHandle(box)
	bootstrapValue[shared.BootstrapExtension](box).OnDestroy()
}

//export envoy_dynamic_module_on_bootstrap_extension_server_initialized
func envoy_dynamic_module_on_bootstrap_extension_server_initialized(
	_ C.envoy_dynamic_module_type_bootstrap_extension_envoy_ptr,
	ptr C.envoy_dynamic_module_type_bootstrap_extension_module_ptr,
) {
	defer recovery.ExportVoid("envoy_dynamic_module_on_bootstrap_extension_server_initialized")
	bootstrapValue[shared.BootstrapExtension](unsafe.Pointer(ptr)).OnServerInitialized()
}

//export envoy_dynamic_module_on_bootstrap_extension_worker_thread_initialized
func envoy_dynamic_module_on_bootstrap_extension_worker_thread_initialized(
	_ C.envoy_dynamic_module_type_bootstrap_extension_envoy_ptr,
	ptr C.envoy_dynamic_module_type_bootstrap_extension_module_ptr,
) {
	defer recovery.ExportVoid("envoy_dynamic_module_on_bootstrap_extension_worker_thread_initialized")
	bootstrapValue[shared.BootstrapExtension](unsafe.Pointer(ptr)).OnWorkerThreadInitialized()
}

//export envoy_dynamic_module_on_bootstrap_extension_drain_started
func envoy_dynamic_module_on_bootstrap_extension_drain_started(
	_ C.envoy_dynamic_module_type_bootstrap_extension_envoy_ptr,
	ptr C.envoy_dynamic_module_type_bootstrap_extension_module_ptr,
) {
	defer recovery.ExportVoid("envoy_dynamic_module_on_bootstrap_extension_drain_started")
	bootstrapValue[shared.BootstrapExtension](unsafe.Pointer(ptr)).OnDrainStarted()
}

//export envoy_dynamic_module_on_bootstrap_extension_shutdown
func envoy_dynamic_module_on_bootstrap_extension_shutdown(
	_ C.envoy_dynamic_module_type_bootstrap_extension_envoy_ptr,
	ptr C.envoy_dynamic_module_type_bootstrap_extension_module_ptr,
	callback C.envoy_dynamic_module_type_event_cb, context unsafe.Pointer,
) {
	defer recovery.ExportVoid("envoy_dynamic_module_on_bootstrap_extension_shutdown")
	var once sync.Once
	complete := func() { once.Do(func() { C.bootstrap_complete(callback, context) }) }
	returned := false
	defer func() {
		if !returned {
			complete()
		}
	}()
	bootstrapValue[shared.BootstrapExtension](unsafe.Pointer(ptr)).OnShutdown(complete)
	returned = true
}

//export envoy_dynamic_module_on_bootstrap_extension_config_scheduled
func envoy_dynamic_module_on_bootstrap_extension_config_scheduled(
	_ C.envoy_dynamic_module_type_bootstrap_extension_config_envoy_ptr,
	ptr C.envoy_dynamic_module_type_bootstrap_extension_config_module_ptr, id C.uint64_t,
) {
	defer recovery.ExportVoid("envoy_dynamic_module_on_bootstrap_extension_config_scheduled")
	wrapper := bootstrapValue[*bootstrapConfigWrapper](unsafe.Pointer(ptr))
	if wrapper.handle.scheduler != nil {
		wrapper.handle.scheduler.scheduler.onScheduled(uint64(id))
	}
}

// Envoy requires these hooks when loading a bootstrap module. The Go SDK does
// not yet expose the corresponding optional capabilities, so none are registered.

//export envoy_dynamic_module_on_bootstrap_extension_http_callout_done
func envoy_dynamic_module_on_bootstrap_extension_http_callout_done(
	host C.envoy_dynamic_module_type_bootstrap_extension_config_envoy_ptr,
	config C.envoy_dynamic_module_type_bootstrap_extension_config_module_ptr,
	id C.uint64_t, result C.envoy_dynamic_module_type_http_callout_result,
	headers *C.envoy_dynamic_module_type_envoy_http_header, headersSize C.size_t,
	body *C.envoy_dynamic_module_type_envoy_buffer, bodySize C.size_t,
) {
}

//export envoy_dynamic_module_on_bootstrap_extension_timer_fired
func envoy_dynamic_module_on_bootstrap_extension_timer_fired(
	host C.envoy_dynamic_module_type_bootstrap_extension_config_envoy_ptr,
	config C.envoy_dynamic_module_type_bootstrap_extension_config_module_ptr,
	timer C.envoy_dynamic_module_type_bootstrap_extension_timer_module_ptr,
) {
}

//export envoy_dynamic_module_on_bootstrap_extension_file_changed
func envoy_dynamic_module_on_bootstrap_extension_file_changed(
	host C.envoy_dynamic_module_type_bootstrap_extension_config_envoy_ptr,
	config C.envoy_dynamic_module_type_bootstrap_extension_config_module_ptr,
	path C.envoy_dynamic_module_type_envoy_buffer, events C.uint32_t,
) {
}

//export envoy_dynamic_module_on_bootstrap_extension_admin_request
func envoy_dynamic_module_on_bootstrap_extension_admin_request(
	host C.envoy_dynamic_module_type_bootstrap_extension_config_envoy_ptr,
	config C.envoy_dynamic_module_type_bootstrap_extension_config_module_ptr,
	method C.envoy_dynamic_module_type_envoy_buffer, path C.envoy_dynamic_module_type_envoy_buffer,
	body C.envoy_dynamic_module_type_envoy_buffer,
) C.uint32_t {
	return 404
}

//export envoy_dynamic_module_on_bootstrap_extension_cluster_add_or_update
func envoy_dynamic_module_on_bootstrap_extension_cluster_add_or_update(
	host C.envoy_dynamic_module_type_bootstrap_extension_config_envoy_ptr,
	config C.envoy_dynamic_module_type_bootstrap_extension_config_module_ptr,
	name C.envoy_dynamic_module_type_envoy_buffer,
) {
}

//export envoy_dynamic_module_on_bootstrap_extension_cluster_removal
func envoy_dynamic_module_on_bootstrap_extension_cluster_removal(
	host C.envoy_dynamic_module_type_bootstrap_extension_config_envoy_ptr,
	config C.envoy_dynamic_module_type_bootstrap_extension_config_module_ptr,
	name C.envoy_dynamic_module_type_envoy_buffer,
) {
}

//export envoy_dynamic_module_on_bootstrap_extension_listener_add_or_update
func envoy_dynamic_module_on_bootstrap_extension_listener_add_or_update(
	host C.envoy_dynamic_module_type_bootstrap_extension_config_envoy_ptr,
	config C.envoy_dynamic_module_type_bootstrap_extension_config_module_ptr,
	name C.envoy_dynamic_module_type_envoy_buffer,
) {
}

//export envoy_dynamic_module_on_bootstrap_extension_listener_removal
func envoy_dynamic_module_on_bootstrap_extension_listener_removal(
	host C.envoy_dynamic_module_type_bootstrap_extension_config_envoy_ptr,
	config C.envoy_dynamic_module_type_bootstrap_extension_config_module_ptr,
	name C.envoy_dynamic_module_type_envoy_buffer,
) {
}
