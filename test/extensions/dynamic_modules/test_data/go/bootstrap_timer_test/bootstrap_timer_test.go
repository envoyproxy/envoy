// Test module for the Bootstrap timer API. Mirrors test_data/rust/bootstrap_timer_test.rs.
//
// Creates two timers during config_new with different per-timer callbacks, arms them with
// short delays, and signals init-complete only after both have fired. The Go SDK uses
// per-timer onFire callbacks (rather than a centralized on_timer_fired hook) so timer
// identity is captured in the closure.
package main

import (
	"sync"
	"sync/atomic"

	sdk "github.com/envoyproxy/envoy/source/extensions/dynamic_modules/sdk/go"
	_ "github.com/envoyproxy/envoy/source/extensions/dynamic_modules/sdk/go/abi"
	"github.com/envoyproxy/envoy/source/extensions/dynamic_modules/sdk/go/shared"
)

func init() {
	sdk.RegisterBootstrapExtensionConfigFactories(map[string]shared.BootstrapExtensionConfigFactory{
		"test": &timerConfigFactory{},
	})
}

func main() {}

type timerConfigFactory struct {
	shared.EmptyBootstrapExtensionConfigFactory
}

func (f *timerConfigFactory) Create(handle shared.BootstrapExtensionConfigHandle, _ []byte) (shared.BootstrapExtension, error) {
	state := &timerTestState{handle: handle}

	timerA := handle.NewTimer(state.onTimerAFired)
	timerB := handle.NewTimer(state.onTimerBFired)

	if timerA == nil || timerB == nil {
		// If either timer can't be created, fall back to signaling init so the test fails
		// loudly via the absence of the expected log lines rather than hanging.
		handle.SignalInitComplete()
		return &shared.EmptyBootstrapExtension{}, nil
	}

	if timerA.Enabled() {
		panic("Timer A should not be enabled upon creation")
	}
	if timerB.Enabled() {
		panic("Timer B should not be enabled upon creation")
	}

	state.mu.Lock()
	state.timerA = timerA
	state.timerB = timerB
	state.mu.Unlock()

	timerA.Enable(10)
	timerB.Enable(20)

	sdk.Log(shared.LogLevelInfo, "Two timers created and armed during config_new")

	// Do NOT signal init here — wait until both timers have fired so the test proves the
	// timer-fired dispatch actually reaches our callbacks.
	return &shared.EmptyBootstrapExtension{}, nil
}

type timerTestState struct {
	handle  shared.BootstrapExtensionConfigHandle
	mu      sync.Mutex
	timerA  shared.BootstrapTimer
	timerB  shared.BootstrapTimer
	aFired  atomic.Bool
	bFired  atomic.Bool
	doneSet atomic.Bool
}

func (s *timerTestState) onTimerAFired(_ shared.BootstrapTimer) {
	sdk.Log(shared.LogLevelInfo, "Timer A fired, identified by callback")
	s.aFired.Store(true)
	s.mu.Lock()
	if s.timerA != nil {
		s.timerA.Delete()
		s.timerA = nil
	}
	s.mu.Unlock()
	s.maybeSignalInitComplete()
}

func (s *timerTestState) onTimerBFired(_ shared.BootstrapTimer) {
	sdk.Log(shared.LogLevelInfo, "Timer B fired, identified by callback")
	s.bFired.Store(true)
	s.mu.Lock()
	if s.timerB != nil {
		s.timerB.Delete()
		s.timerB = nil
	}
	s.mu.Unlock()
	s.maybeSignalInitComplete()
}

func (s *timerTestState) maybeSignalInitComplete() {
	if !s.aFired.Load() || !s.bFired.Load() {
		return
	}
	if s.doneSet.Swap(true) {
		return
	}
	s.handle.SignalInitComplete()
	sdk.Log(shared.LogLevelInfo, "Bootstrap timer test completed successfully!")
}
