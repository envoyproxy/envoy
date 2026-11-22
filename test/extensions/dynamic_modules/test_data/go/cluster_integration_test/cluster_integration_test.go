package main

/*
#include <stdint.h>
*/
import "C"

import (
	"fmt"
	"runtime"
	"sync"
	"sync/atomic"
	"time"

	sdk "github.com/envoyproxy/envoy/source/extensions/dynamic_modules/sdk/go"
	_ "github.com/envoyproxy/envoy/source/extensions/dynamic_modules/sdk/go/abi"
	"github.com/envoyproxy/envoy/source/extensions/dynamic_modules/sdk/go/shared"
)

func init() {
	sdk.RegisterClusterConfigFactories(map[string]shared.ClusterConfigFactory{"test": &configFactory{}})
}

func main() {}

type configFactory struct{}
type config struct {
	mode   string
	handle shared.ClusterConfigHandle
}

func (*configFactory) Create(handle shared.ClusterConfigHandle, data []byte) (shared.ClusterConfig, error) {
	mode := string(data)
	switch mode {
	case "create_error":
		return nil, fmt.Errorf("cluster config rejected")
	case "create_nil":
		return nil, nil
	case "create_panic":
		panic("cluster config panic")
	}
	return &config{mode: mode, handle: handle}, nil
}

func (c *config) NewCluster(handle shared.ClusterHandle) shared.Cluster {
	switch c.mode {
	case "new_nil":
		handle.GetScheduler()
		return nil
	case "new_panic":
		handle.GetScheduler()
		panic("cluster instance panic")
	}
	instance := &cluster{handle: handle, mode: c.mode,
		release: make(chan struct{}), partial: make(chan struct{}), complete: make(chan struct{})}
	current.Store(instance)
	return instance
}

func (c *config) OnDestroy() {
	c.handle.Log(shared.LogLevelInfo, "Go cluster config destroyed")
	if c.mode == "destroy_panic" {
		panic("cluster config destroy panic")
	}
}

const workers = 4
const eventID = 42

var current atomic.Pointer[cluster]

type cluster struct {
	shared.EmptyCluster
	serverReady                atomic.Bool
	started                    atomic.Bool
	handle                     shared.ClusterHandle
	scheduler                  shared.Scheduler
	mode                       string
	arrivals                   atomic.Uint32
	completions                atomic.Uint32
	release, partial, complete chan struct{}
	releaseOnce                sync.Once
}

func (c *cluster) OnInit() {
	c.scheduler = c.handle.GetScheduler()
	if c.mode == "init_sync" {
		c.handle.PreInitComplete()
		return
	}
	go c.scheduler.Schedule(func() {
		runtime.GC()
		c.scheduler.Schedule(c.handle.PreInitComplete)
	})
}

func (c *cluster) OnServerInitialized() {
	c.serverReady.Store(true)
}

//export go_cluster_test_start_worker_events
func go_cluster_test_start_worker_events() bool {
	c := current.Load()
	if c == nil || !c.serverReady.Load() || !c.started.CompareAndSwap(false, true) {
		return false
	}
	go c.scheduler.Schedule(func() { c.handle.RunOnAllWorkers(eventID) })
	return true
}

func (c *cluster) OnWorkerEvent(id uint64) {
	if id != eventID {
		panic("unexpected cluster worker event")
	}
	if c.arrivals.Add(1) == 1 && c.mode == "blocked" {
		<-c.release
	}
	c.scheduler.Schedule(func() {
		switch c.completions.Add(1) {
		case workers - 1:
			close(c.partial)
		case workers:
			close(c.complete)
		}
	})
}

func (c *cluster) OnShutdown(complete func()) {
	if c.mode == "shutdown_panic" {
		panic("cluster shutdown panic")
	}
	go func() { complete(); complete() }()
}

func (c *cluster) OnDestroy() {
	c.handle.Log(shared.LogLevelInfo, "Go cluster instance destroyed")
	if c.mode == "destroy_panic" {
		panic("cluster instance destroy panic")
	}
}

// These hooks let the native test wait for pushed events without polling or sleeps.
//
//export go_cluster_test_wait_for_stage
func go_cluster_test_wait_for_stage(stage, timeoutMs C.uint32_t) bool {
	c := current.Load()
	if c == nil {
		return false
	}
	ready := c.complete
	if stage == 0 {
		ready = c.partial
	}
	timeout := time.NewTimer(time.Duration(timeoutMs) * time.Millisecond)
	defer timeout.Stop()
	select {
	case <-ready:
		return true
	case <-timeout.C:
		return false
	}
}

//export go_cluster_test_release_worker
func go_cluster_test_release_worker() {
	if c := current.Load(); c != nil {
		c.releaseOnce.Do(func() { close(c.release) })
	}
}

//export go_cluster_test_completion_count
func go_cluster_test_completion_count() C.uint32_t {
	if c := current.Load(); c != nil {
		return C.uint32_t(c.completions.Load())
	}
	return 0
}

//export go_cluster_test_schedule_after_destroy
func go_cluster_test_schedule_after_destroy() {
	current.Load().scheduler.Schedule(func() { panic("task ran after cluster destruction") })
}
