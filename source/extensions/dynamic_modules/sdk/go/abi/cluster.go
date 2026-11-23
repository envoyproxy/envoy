//go:build !envoy_dynamic_modules_go_custom_cluster

package abi

/*
#include <stdint.h>
#include "../../../abi/abi.h"

static inline void cluster_complete(envoy_dynamic_module_type_event_cb callback, void* context) {
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

type dymClusterConfigHandle struct{ dymCommonHandle }

func (dymClusterConfigHandle) Log(level shared.LogLevel, format string, args ...any) {
	hostLog(level, format, args)
}

type clusterWrapper struct {
	cluster shared.Cluster
	handle  *dymClusterHandle
}

type dymClusterHandle struct {
	host      C.envoy_dynamic_module_type_cluster_envoy_ptr
	scheduler scheduler.Scheduler
}

func (*dymClusterHandle) Log(level shared.LogLevel, format string, args ...any) {
	hostLog(level, format, args)
}

func (h *dymClusterHandle) PreInitComplete() {
	C.envoy_dynamic_module_callback_cluster_pre_init_complete(h.host)
}

func (h *dymClusterHandle) RunOnAllWorkers(eventID uint64) {
	C.envoy_dynamic_module_callback_cluster_run_on_all_workers(h.host, C.uint64_t(eventID))
}

func (h *dymClusterHandle) GetScheduler() shared.Scheduler {
	h.scheduler.Init(func() (func(uint64), func()) {
		ptr := C.envoy_dynamic_module_callback_cluster_scheduler_new(h.host)
		if ptr == nil {
			panic("creating cluster scheduler")
		}
		commit := func(id uint64) {
			C.envoy_dynamic_module_callback_cluster_scheduler_commit(ptr, C.uint64_t(id))
		}
		destroy := func() {
			C.envoy_dynamic_module_callback_cluster_scheduler_delete(ptr)
		}
		return commit, destroy
	})
	return &h.scheduler
}

//export envoy_dynamic_module_on_cluster_config_new
func envoy_dynamic_module_on_cluster_config_new(
	_ C.envoy_dynamic_module_type_cluster_config_envoy_ptr,
	name, config C.envoy_dynamic_module_type_envoy_buffer,
) (result C.envoy_dynamic_module_type_cluster_config_module_ptr) {
	defer recovery.Export("envoy_dynamic_module_on_cluster_config_new", nil, &result)
	handle := dymClusterConfigHandle{}
	nameString := envoyBufferToStringUnsafe(name)
	clusterConfig, err := sdk.NewClusterConfig(handle, nameString, envoyBufferToBytesUnsafe(config))
	if err != nil {
		handle.Log(shared.LogLevelWarn, "Failed to load cluster configuration for %q: %v", nameString, err)
		return nil
	}
	if clusterConfig == nil {
		handle.Log(shared.LogLevelWarn, "Failed to load cluster configuration for %q: config is nil", nameString)
		return nil
	}
	return C.envoy_dynamic_module_type_cluster_config_module_ptr(newHandle(clusterConfig))
}

//export envoy_dynamic_module_on_cluster_config_destroy
func envoy_dynamic_module_on_cluster_config_destroy(ptr C.envoy_dynamic_module_type_cluster_config_module_ptr) {
	defer recovery.ExportVoid("envoy_dynamic_module_on_cluster_config_destroy")
	box := unsafe.Pointer(ptr)
	defer deleteHandle(box)
	handleValue[shared.ClusterConfig](box).OnDestroy()
}

//export envoy_dynamic_module_on_cluster_new
func envoy_dynamic_module_on_cluster_new(
	ptr C.envoy_dynamic_module_type_cluster_config_module_ptr,
	host C.envoy_dynamic_module_type_cluster_envoy_ptr,
) (result C.envoy_dynamic_module_type_cluster_module_ptr) {
	defer recovery.Export("envoy_dynamic_module_on_cluster_new", nil, &result)
	handle := &dymClusterHandle{host: host}
	retained := false
	defer func() {
		if !retained {
			handle.scheduler.Close()
		}
	}()
	cluster := handleValue[shared.ClusterConfig](unsafe.Pointer(ptr)).Create(handle)
	if cluster == nil {
		return nil
	}
	result = C.envoy_dynamic_module_type_cluster_module_ptr(
		newHandle(&clusterWrapper{cluster: cluster, handle: handle}))
	retained = true
	return result
}

//export envoy_dynamic_module_on_cluster_destroy
func envoy_dynamic_module_on_cluster_destroy(ptr C.envoy_dynamic_module_type_cluster_module_ptr) {
	defer recovery.ExportVoid("envoy_dynamic_module_on_cluster_destroy")
	box := unsafe.Pointer(ptr)
	wrapper := handleValue[*clusterWrapper](box)
	defer deleteHandle(box)
	defer wrapper.handle.scheduler.Close()
	wrapper.cluster.OnDestroy()
}

//export envoy_dynamic_module_on_cluster_init
func envoy_dynamic_module_on_cluster_init(
	_ C.envoy_dynamic_module_type_cluster_envoy_ptr,
	ptr C.envoy_dynamic_module_type_cluster_module_ptr,
) {
	defer recovery.ExportVoid("envoy_dynamic_module_on_cluster_init")
	handleValue[*clusterWrapper](unsafe.Pointer(ptr)).cluster.OnInit()
}

//export envoy_dynamic_module_on_cluster_server_initialized
func envoy_dynamic_module_on_cluster_server_initialized(
	_ C.envoy_dynamic_module_type_cluster_envoy_ptr,
	ptr C.envoy_dynamic_module_type_cluster_module_ptr,
) {
	defer recovery.ExportVoid("envoy_dynamic_module_on_cluster_server_initialized")
	handleValue[*clusterWrapper](unsafe.Pointer(ptr)).cluster.OnServerInitialized()
}

//export envoy_dynamic_module_on_cluster_drain_started
func envoy_dynamic_module_on_cluster_drain_started(
	_ C.envoy_dynamic_module_type_cluster_envoy_ptr,
	ptr C.envoy_dynamic_module_type_cluster_module_ptr,
) {
	defer recovery.ExportVoid("envoy_dynamic_module_on_cluster_drain_started")
	handleValue[*clusterWrapper](unsafe.Pointer(ptr)).cluster.OnDrainStarted()
}

//export envoy_dynamic_module_on_cluster_scheduled
func envoy_dynamic_module_on_cluster_scheduled(
	_ C.envoy_dynamic_module_type_cluster_envoy_ptr,
	ptr C.envoy_dynamic_module_type_cluster_module_ptr, id C.uint64_t,
) {
	defer recovery.ExportVoid("envoy_dynamic_module_on_cluster_scheduled")
	wrapper := handleValue[*clusterWrapper](unsafe.Pointer(ptr))
	wrapper.handle.scheduler.Run(uint64(id))
}

//export envoy_dynamic_module_on_cluster_worker_event
func envoy_dynamic_module_on_cluster_worker_event(
	_ C.envoy_dynamic_module_type_cluster_envoy_ptr,
	ptr C.envoy_dynamic_module_type_cluster_module_ptr, id C.uint64_t,
) {
	defer recovery.ExportVoid("envoy_dynamic_module_on_cluster_worker_event")
	handleValue[*clusterWrapper](unsafe.Pointer(ptr)).cluster.OnWorkerEvent(uint64(id))
}

//export envoy_dynamic_module_on_cluster_shutdown
func envoy_dynamic_module_on_cluster_shutdown(
	_ C.envoy_dynamic_module_type_cluster_envoy_ptr,
	ptr C.envoy_dynamic_module_type_cluster_module_ptr,
	callback C.envoy_dynamic_module_type_event_cb, context unsafe.Pointer,
) {
	defer recovery.ExportVoid("envoy_dynamic_module_on_cluster_shutdown")
	var once sync.Once
	complete := func() { once.Do(func() { C.cluster_complete(callback, context) }) }
	returned := false
	defer func() {
		if !returned {
			complete()
		}
	}()
	handleValue[*clusterWrapper](unsafe.Pointer(ptr)).cluster.OnShutdown(complete)
	returned = true
}

// The native loader requires these hooks even when the module uses Envoy's load balancer.
//
//export envoy_dynamic_module_on_cluster_lb_new
func envoy_dynamic_module_on_cluster_lb_new(
	cluster C.envoy_dynamic_module_type_cluster_module_ptr,
	lb C.envoy_dynamic_module_type_cluster_lb_envoy_ptr,
) C.envoy_dynamic_module_type_cluster_lb_module_ptr {
	return nil
}

//export envoy_dynamic_module_on_cluster_lb_destroy
func envoy_dynamic_module_on_cluster_lb_destroy(_ C.envoy_dynamic_module_type_cluster_lb_module_ptr) {
}

//export envoy_dynamic_module_on_cluster_lb_choose_host
func envoy_dynamic_module_on_cluster_lb_choose_host(
	lb C.envoy_dynamic_module_type_cluster_lb_module_ptr,
	context C.envoy_dynamic_module_type_cluster_lb_context_envoy_ptr,
	host *C.envoy_dynamic_module_type_cluster_host_envoy_ptr,
	async *C.envoy_dynamic_module_type_cluster_lb_async_handle_module_ptr,
) {
	*host = nil
	*async = nil
}
