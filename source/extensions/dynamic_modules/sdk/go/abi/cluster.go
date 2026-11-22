package abi

/*
#include <stdint.h>
#include <stdlib.h>
#include "../../../abi/abi.h"

static inline void cluster_complete(envoy_dynamic_module_type_event_cb callback, void* context) {
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
func newClusterHandle(value any) unsafe.Pointer {
	box := C.malloc(C.size_t(unsafe.Sizeof(C.uintptr_t(0))))
	if box == nil {
		panic("allocating cluster handle")
	}
	*(*C.uintptr_t)(box) = C.uintptr_t(cgo.NewHandle(value))
	return box
}

func clusterValue[T any](box unsafe.Pointer) T {
	return cgo.Handle(*(*C.uintptr_t)(box)).Value().(T)
}

func deleteClusterHandle(box unsafe.Pointer) {
	cgo.Handle(*(*C.uintptr_t)(box)).Delete()
	C.free(box)
}

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
	scheduler *clusterScheduler
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
	if h.scheduler == nil {
		ptr := C.envoy_dynamic_module_callback_cluster_scheduler_new(h.host)
		if ptr == nil {
			panic("creating cluster scheduler")
		}
		h.scheduler = &clusterScheduler{scheduler: newDymScheduler(unsafe.Pointer(ptr),
			func(ptr unsafe.Pointer, id C.uint64_t) {
				C.envoy_dynamic_module_callback_cluster_scheduler_commit(
					C.envoy_dynamic_module_type_cluster_scheduler_module_ptr(ptr), id)
			})}
	}
	return h.scheduler
}

func (h *dymClusterHandle) close() {
	if h.scheduler != nil {
		h.scheduler.close()
	}
}

// A native commit can release the last reference to the cluster and re-enter
// its destroy hook. Keep the scheduler alive across commits without holding a lock
// across the ABI call or waiting for those commits during destruction.
type clusterScheduler struct {
	mu        sync.Mutex
	scheduler *dymScheduler
	closed    bool
	active    uint64
}

func (s *clusterScheduler) Schedule(task func()) {
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

func (s *clusterScheduler) close() {
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
func (s *clusterScheduler) destroy() {
	C.envoy_dynamic_module_callback_cluster_scheduler_delete(
		C.envoy_dynamic_module_type_cluster_scheduler_module_ptr(s.scheduler.schedulerPtr))
	s.scheduler.schedulerLock.Lock()
	clear(s.scheduler.tasks)
	s.scheduler.schedulerLock.Unlock()
}

//export envoy_dynamic_module_on_cluster_config_new
func envoy_dynamic_module_on_cluster_config_new(
	_ C.envoy_dynamic_module_type_cluster_config_envoy_ptr,
	name, config C.envoy_dynamic_module_type_envoy_buffer,
) (result C.envoy_dynamic_module_type_cluster_config_module_ptr) {
	defer recovery.Export("envoy_dynamic_module_on_cluster_config_new", nil, &result)
	handle := dymClusterConfigHandle{}
	clusterConfig, err := sdk.NewClusterConfig(handle,
		envoyBufferToStringUnsafe(name), envoyBufferToBytesUnsafe(config))
	if err != nil || clusterConfig == nil {
		handle.Log(shared.LogLevelWarn, "Failed to load cluster configuration: %v", err)
		return nil
	}
	return C.envoy_dynamic_module_type_cluster_config_module_ptr(newClusterHandle(clusterConfig))
}

//export envoy_dynamic_module_on_cluster_config_destroy
func envoy_dynamic_module_on_cluster_config_destroy(ptr C.envoy_dynamic_module_type_cluster_config_module_ptr) {
	defer recovery.ExportVoid("envoy_dynamic_module_on_cluster_config_destroy")
	box := unsafe.Pointer(ptr)
	defer deleteClusterHandle(box)
	clusterValue[shared.ClusterConfig](box).OnDestroy()
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
			handle.close()
		}
	}()
	cluster := clusterValue[shared.ClusterConfig](unsafe.Pointer(ptr)).NewCluster(handle)
	if cluster == nil {
		return nil
	}
	result = C.envoy_dynamic_module_type_cluster_module_ptr(
		newClusterHandle(&clusterWrapper{cluster: cluster, handle: handle}))
	retained = true
	return result
}

//export envoy_dynamic_module_on_cluster_destroy
func envoy_dynamic_module_on_cluster_destroy(ptr C.envoy_dynamic_module_type_cluster_module_ptr) {
	defer recovery.ExportVoid("envoy_dynamic_module_on_cluster_destroy")
	box := unsafe.Pointer(ptr)
	wrapper := clusterValue[*clusterWrapper](box)
	defer deleteClusterHandle(box)
	defer wrapper.handle.close()
	wrapper.cluster.OnDestroy()
}

//export envoy_dynamic_module_on_cluster_init
func envoy_dynamic_module_on_cluster_init(
	_ C.envoy_dynamic_module_type_cluster_envoy_ptr,
	ptr C.envoy_dynamic_module_type_cluster_module_ptr,
) {
	defer recovery.ExportVoid("envoy_dynamic_module_on_cluster_init")
	clusterValue[*clusterWrapper](unsafe.Pointer(ptr)).cluster.OnInit()
}

//export envoy_dynamic_module_on_cluster_server_initialized
func envoy_dynamic_module_on_cluster_server_initialized(
	_ C.envoy_dynamic_module_type_cluster_envoy_ptr,
	ptr C.envoy_dynamic_module_type_cluster_module_ptr,
) {
	defer recovery.ExportVoid("envoy_dynamic_module_on_cluster_server_initialized")
	clusterValue[*clusterWrapper](unsafe.Pointer(ptr)).cluster.OnServerInitialized()
}

//export envoy_dynamic_module_on_cluster_drain_started
func envoy_dynamic_module_on_cluster_drain_started(
	_ C.envoy_dynamic_module_type_cluster_envoy_ptr,
	ptr C.envoy_dynamic_module_type_cluster_module_ptr,
) {
	defer recovery.ExportVoid("envoy_dynamic_module_on_cluster_drain_started")
	clusterValue[*clusterWrapper](unsafe.Pointer(ptr)).cluster.OnDrainStarted()
}

//export envoy_dynamic_module_on_cluster_scheduled
func envoy_dynamic_module_on_cluster_scheduled(
	_ C.envoy_dynamic_module_type_cluster_envoy_ptr,
	ptr C.envoy_dynamic_module_type_cluster_module_ptr, id C.uint64_t,
) {
	defer recovery.ExportVoid("envoy_dynamic_module_on_cluster_scheduled")
	wrapper := clusterValue[*clusterWrapper](unsafe.Pointer(ptr))
	if wrapper.handle.scheduler != nil {
		wrapper.handle.scheduler.scheduler.onScheduled(uint64(id))
	}
}

//export envoy_dynamic_module_on_cluster_worker_event
func envoy_dynamic_module_on_cluster_worker_event(
	_ C.envoy_dynamic_module_type_cluster_envoy_ptr,
	ptr C.envoy_dynamic_module_type_cluster_module_ptr, id C.uint64_t,
) {
	defer recovery.ExportVoid("envoy_dynamic_module_on_cluster_worker_event")
	clusterValue[*clusterWrapper](unsafe.Pointer(ptr)).cluster.OnWorkerEvent(uint64(id))
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
	clusterValue[*clusterWrapper](unsafe.Pointer(ptr)).cluster.OnShutdown(complete)
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
