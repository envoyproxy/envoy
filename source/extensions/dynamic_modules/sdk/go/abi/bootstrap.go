//go:build !envoy_dynamic_modules_go_custom_bootstrap

package abi

/*
#include <stdint.h>
#include "../../../abi/abi.h"

static inline void bootstrap_complete(envoy_dynamic_module_type_event_cb callback, void* context) {
  callback(context);
}
*/
import "C"

import (
	"sync"
	"unsafe"

	sdk "github.com/envoyproxy/envoy/source/extensions/dynamic_modules/sdk/go"
	"github.com/envoyproxy/envoy/source/extensions/dynamic_modules/sdk/go/internal/recovery"
	"github.com/envoyproxy/envoy/source/extensions/dynamic_modules/sdk/go/internal/scheduler"
	"github.com/envoyproxy/envoy/source/extensions/dynamic_modules/sdk/go/shared"
)

type bootstrapConfigWrapper struct {
	config shared.BootstrapExtensionConfig
	handle *dymBootstrapConfigHandle
}

type dymBootstrapConfigHandle struct {
	dymCommonHandle
	host      C.envoy_dynamic_module_type_bootstrap_extension_config_envoy_ptr
	scheduler scheduler.Scheduler
}

func (h *dymBootstrapConfigHandle) Log(level shared.LogLevel, format string, args ...any) {
	hostLog(level, format, args)
}

func (h *dymBootstrapConfigHandle) SignalInitComplete() {
	C.envoy_dynamic_module_callback_bootstrap_extension_config_signal_init_complete(h.host)
}

func (h *dymBootstrapConfigHandle) GetScheduler() shared.Scheduler {
	h.scheduler.Init(func() (func(uint64), func()) {
		ptr := C.envoy_dynamic_module_callback_bootstrap_extension_config_scheduler_new(h.host)
		if ptr == nil {
			panic("creating bootstrap scheduler")
		}
		commit := func(id uint64) {
			C.envoy_dynamic_module_callback_bootstrap_extension_config_scheduler_commit(ptr, C.uint64_t(id))
		}
		destroy := func() {
			C.envoy_dynamic_module_callback_bootstrap_extension_config_scheduler_delete(ptr)
		}
		return commit, destroy
	})
	return &h.scheduler
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
			handle.scheduler.Close()
		}
	}()
	nameString := envoyBufferToStringUnsafe(name)
	extensionConfig, err := sdk.NewBootstrapExtensionConfig(handle, nameString, envoyBufferToBytesUnsafe(config))
	if err != nil {
		handle.Log(shared.LogLevelWarn, "Failed to load bootstrap configuration for %q: %v", nameString, err)
		return nil
	}
	if extensionConfig == nil {
		handle.Log(shared.LogLevelWarn, "Failed to load bootstrap configuration for %q: config is nil", nameString)
		return nil
	}
	result = C.envoy_dynamic_module_type_bootstrap_extension_config_module_ptr(
		newHandle(&bootstrapConfigWrapper{config: extensionConfig, handle: handle}))
	retained = true
	return result
}

//export envoy_dynamic_module_on_bootstrap_extension_config_destroy
func envoy_dynamic_module_on_bootstrap_extension_config_destroy(ptr C.envoy_dynamic_module_type_bootstrap_extension_config_module_ptr) {
	defer recovery.ExportVoid("envoy_dynamic_module_on_bootstrap_extension_config_destroy")
	box := unsafe.Pointer(ptr)
	wrapper := handleValue[*bootstrapConfigWrapper](box)
	defer deleteHandle(box)
	defer wrapper.handle.scheduler.Close()
	wrapper.config.OnDestroy()
}

//export envoy_dynamic_module_on_bootstrap_extension_new
func envoy_dynamic_module_on_bootstrap_extension_new(
	ptr C.envoy_dynamic_module_type_bootstrap_extension_config_module_ptr,
	_ C.envoy_dynamic_module_type_bootstrap_extension_envoy_ptr,
) (result C.envoy_dynamic_module_type_bootstrap_extension_module_ptr) {
	defer recovery.Export("envoy_dynamic_module_on_bootstrap_extension_new", nil, &result)
	wrapper := handleValue[*bootstrapConfigWrapper](unsafe.Pointer(ptr))
	extension := wrapper.config.Create(dymBootstrapExtensionHandle{})
	if extension == nil {
		return nil
	}
	return C.envoy_dynamic_module_type_bootstrap_extension_module_ptr(newHandle(extension))
}

//export envoy_dynamic_module_on_bootstrap_extension_destroy
func envoy_dynamic_module_on_bootstrap_extension_destroy(ptr C.envoy_dynamic_module_type_bootstrap_extension_module_ptr) {
	defer recovery.ExportVoid("envoy_dynamic_module_on_bootstrap_extension_destroy")
	box := unsafe.Pointer(ptr)
	defer deleteHandle(box)
	handleValue[shared.BootstrapExtension](box).OnDestroy()
}

//export envoy_dynamic_module_on_bootstrap_extension_server_initialized
func envoy_dynamic_module_on_bootstrap_extension_server_initialized(
	_ C.envoy_dynamic_module_type_bootstrap_extension_envoy_ptr,
	ptr C.envoy_dynamic_module_type_bootstrap_extension_module_ptr,
) {
	defer recovery.ExportVoid("envoy_dynamic_module_on_bootstrap_extension_server_initialized")
	handleValue[shared.BootstrapExtension](unsafe.Pointer(ptr)).OnServerInitialized()
}

//export envoy_dynamic_module_on_bootstrap_extension_worker_thread_initialized
func envoy_dynamic_module_on_bootstrap_extension_worker_thread_initialized(
	_ C.envoy_dynamic_module_type_bootstrap_extension_envoy_ptr,
	ptr C.envoy_dynamic_module_type_bootstrap_extension_module_ptr,
) {
	defer recovery.ExportVoid("envoy_dynamic_module_on_bootstrap_extension_worker_thread_initialized")
	handleValue[shared.BootstrapExtension](unsafe.Pointer(ptr)).OnWorkerThreadInitialized()
}

//export envoy_dynamic_module_on_bootstrap_extension_drain_started
func envoy_dynamic_module_on_bootstrap_extension_drain_started(
	_ C.envoy_dynamic_module_type_bootstrap_extension_envoy_ptr,
	ptr C.envoy_dynamic_module_type_bootstrap_extension_module_ptr,
) {
	defer recovery.ExportVoid("envoy_dynamic_module_on_bootstrap_extension_drain_started")
	handleValue[shared.BootstrapExtension](unsafe.Pointer(ptr)).OnDrainStarted()
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
	handleValue[shared.BootstrapExtension](unsafe.Pointer(ptr)).OnShutdown(complete)
	returned = true
}

//export envoy_dynamic_module_on_bootstrap_extension_config_scheduled
func envoy_dynamic_module_on_bootstrap_extension_config_scheduled(
	_ C.envoy_dynamic_module_type_bootstrap_extension_config_envoy_ptr,
	ptr C.envoy_dynamic_module_type_bootstrap_extension_config_module_ptr, id C.uint64_t,
) {
	defer recovery.ExportVoid("envoy_dynamic_module_on_bootstrap_extension_config_scheduled")
	wrapper := handleValue[*bootstrapConfigWrapper](unsafe.Pointer(ptr))
	wrapper.handle.scheduler.Run(uint64(id))
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
