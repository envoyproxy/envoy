package main

import "C"

import (
	"fmt"
	"runtime"

	sdk "github.com/envoyproxy/envoy/source/extensions/dynamic_modules/sdk/go"
	_ "github.com/envoyproxy/envoy/source/extensions/dynamic_modules/sdk/go/abi"
	"github.com/envoyproxy/envoy/source/extensions/dynamic_modules/sdk/go/shared"
)

func init() {
	sdk.RegisterBootstrapExtensionConfigFactories(map[string]shared.BootstrapExtensionConfigFactory{
		"test": &configFactory{},
	})
}

func main() {}

type configFactory struct{}

var schedulerCommitDone chan struct{}

//export go_bootstrap_test_wait_for_scheduler
func go_bootstrap_test_wait_for_scheduler() {
	<-schedulerCommitDone
}

func (*configFactory) Create(handle shared.BootstrapExtensionConfigHandle, data []byte) (shared.BootstrapExtensionConfig, error) {
	config := &bootstrapConfig{handle: handle, scheduler: handle.GetScheduler(), mode: string(data)}
	switch config.mode {
	case "create_error":
		return nil, fmt.Errorf("bootstrap config rejected")
	case "create_panic":
		panic("bootstrap config panic")
	case "create_nil":
		return nil, nil
	case "new_nil", "new_panic":
		return config, nil
	case "commit_teardown":
		schedulerCommitDone = make(chan struct{})
		go func() {
			config.scheduler.Schedule(func() {})
			close(schedulerCommitDone)
		}()
		return config, nil
	}
	// Cross a goroutine boundary, then schedule again from inside the callback.
	// Initialization cannot finish until both tasks have run on the main thread.
	go config.scheduler.Schedule(func() {
		runtime.GC()
		handle.Log(shared.LogLevelInfo, "Go bootstrap scheduled initialization")
		config.scheduler.Schedule(func() {
			handle.Log(shared.LogLevelInfo, "Go bootstrap initialization complete")
			handle.SignalInitComplete()
		})
	})
	return config, nil
}

type bootstrapConfig struct {
	handle    shared.BootstrapExtensionConfigHandle
	scheduler shared.Scheduler
	mode      string
}

func (c *bootstrapConfig) NewBootstrapExtension(handle shared.BootstrapExtensionHandle) shared.BootstrapExtension {
	runtime.GC()
	switch c.mode {
	case "new_nil":
		return nil
	case "new_panic":
		panic("bootstrap instance panic")
	case "default":
		return &shared.EmptyBootstrapExtension{}
	}
	return &bootstrapExtension{handle: handle, mode: c.mode}
}

func (c *bootstrapConfig) OnDestroy() {
	c.handle.Log(shared.LogLevelInfo, "Go bootstrap config destroyed")
	if c.mode == "destroy_panic" {
		panic("bootstrap config destroy panic")
	}
}

type bootstrapExtension struct {
	shared.EmptyBootstrapExtension
	handle shared.BootstrapExtensionHandle
	mode   string
}

func (e *bootstrapExtension) OnServerInitialized() {
	runtime.GC()
	e.handle.Log(shared.LogLevelInfo, "Go bootstrap server initialized")
}

func (e *bootstrapExtension) OnWorkerThreadInitialized() {
	e.handle.Log(shared.LogLevelInfo, "Go bootstrap workers initialized")
}

func (e *bootstrapExtension) OnDrainStarted() {
	e.handle.Log(shared.LogLevelInfo, "Go bootstrap drain started")
}

func (e *bootstrapExtension) OnShutdown(complete func()) {
	if e.mode == "shutdown_panic" {
		panic("bootstrap shutdown panic")
	}
	go func() {
		e.handle.Log(shared.LogLevelInfo, "Go bootstrap asynchronous shutdown")
		complete()
		complete() // The SDK must call Envoy's completion exactly once.
	}()
}

func (e *bootstrapExtension) OnDestroy() {
	e.handle.Log(shared.LogLevelInfo, "Go bootstrap instance destroyed")
	if e.mode == "destroy_panic" {
		panic("bootstrap instance destroy panic")
	}
}
