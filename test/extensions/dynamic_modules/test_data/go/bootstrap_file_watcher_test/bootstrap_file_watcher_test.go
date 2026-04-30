// Test module for the Bootstrap file-watcher API. Mirrors
// test_data/rust/bootstrap_file_watcher_test.rs.
//
// Config format: two file paths separated by `|`. The module:
//  1. Adds a watch on each path via AddFileWatch.
//  2. Schedules three timer-driven writes (timer A → file_a; timer B → file_b;
//     timer C → file_a again) so the watcher must report at least 2 changes on file_a
//     and 1 change on file_b.
//  3. Defers SignalInitComplete until all expected change counts are seen, proving the
//     watch dispatch path works end-to-end.
package main

import (
	"os"
	"strings"
	"sync"
	"sync/atomic"

	sdk "github.com/envoyproxy/envoy/source/extensions/dynamic_modules/sdk/go"
	_ "github.com/envoyproxy/envoy/source/extensions/dynamic_modules/sdk/go/abi"
	"github.com/envoyproxy/envoy/source/extensions/dynamic_modules/sdk/go/shared"
)

func init() {
	sdk.RegisterBootstrapExtensionConfigFactories(map[string]shared.BootstrapExtensionConfigFactory{
		"test": &fileWatcherConfigFactory{},
	})
}

func main() {}

type fileWatcherConfigFactory struct {
	shared.EmptyBootstrapExtensionConfigFactory
}

func (f *fileWatcherConfigFactory) Create(handle shared.BootstrapExtensionConfigHandle, config []byte) (shared.BootstrapExtension, error) {
	parts := strings.Split(string(config), "|")
	if len(parts) != 2 {
		panic("bootstrap_file_watcher_test: config must be two paths separated by |")
	}
	pathA, pathB := parts[0], parts[1]
	sdk.Log(shared.LogLevelInfo, "Watching file_a=%s and file_b=%s", pathA, pathB)

	state := &fileWatcherState{handle: handle, pathA: pathA, pathB: pathB}

	events := shared.FileWatcherEventMovedTo | shared.FileWatcherEventModified
	if !handle.AddFileWatch(pathA, events, state.onAChanged) {
		panic("add_file_watch for file_a must succeed")
	}
	if !handle.AddFileWatch(pathB, events, state.onBChanged) {
		panic("add_file_watch for file_b must succeed")
	}
	sdk.Log(shared.LogLevelInfo, "File watches added for 2 files")

	// Schedule three timer-driven writes. Each timer carries its own onFire closure since the
	// Go SDK uses per-timer callbacks rather than a centralized on_timer_fired hook.
	timerA := handle.NewTimer(func(t shared.BootstrapTimer) {
		sdk.Log(shared.LogLevelInfo, "Timer A fired: writing to file_a=%s", pathA)
		if err := os.WriteFile(pathA, []byte("change 1 by timer_a"), 0644); err != nil {
			sdk.Log(shared.LogLevelError, "Failed to write to file_a: %v", err)
		}
		t.Delete()
	})
	timerB := handle.NewTimer(func(t shared.BootstrapTimer) {
		sdk.Log(shared.LogLevelInfo, "Timer B fired: writing to file_b=%s", pathB)
		if err := os.WriteFile(pathB, []byte("change 1 by timer_b"), 0644); err != nil {
			sdk.Log(shared.LogLevelError, "Failed to write to file_b: %v", err)
		}
		t.Delete()
	})
	timerC := handle.NewTimer(func(t shared.BootstrapTimer) {
		sdk.Log(shared.LogLevelInfo, "Timer C fired: writing to file_a again=%s", pathA)
		if err := os.WriteFile(pathA, []byte("change 2 by timer_c"), 0644); err != nil {
			sdk.Log(shared.LogLevelError, "Failed to write to file_a again: %v", err)
		}
		t.Delete()
	})
	timerA.Enable(10)
	timerB.Enable(100)
	timerC.Enable(200)

	// Defer SignalInitComplete to onAChanged/onBChanged once the expected change counts
	// have been observed.
	return &shared.EmptyBootstrapExtension{}, nil
}

type fileWatcherState struct {
	handle       shared.BootstrapExtensionConfigHandle
	pathA        string
	pathB        string
	mu           sync.Mutex
	aCount       atomic.Uint32
	bCount       atomic.Uint32
	initSignaled atomic.Bool
}

func (s *fileWatcherState) onAChanged(path string, events shared.FileWatcherEvent) {
	count := s.aCount.Add(1)
	sdk.Log(shared.LogLevelInfo, "file_a changed: path=%s, events=0x%x, count=%d", path, uint32(events), count)
	s.maybeSignalInitComplete()
}

func (s *fileWatcherState) onBChanged(path string, events shared.FileWatcherEvent) {
	count := s.bCount.Add(1)
	sdk.Log(shared.LogLevelInfo, "file_b changed: path=%s, events=0x%x, count=%d", path, uint32(events), count)
	s.maybeSignalInitComplete()
}

func (s *fileWatcherState) maybeSignalInitComplete() {
	a := s.aCount.Load()
	b := s.bCount.Load()
	if a < 2 || b < 1 {
		return
	}
	if s.initSignaled.Swap(true) {
		return
	}
	sdk.Log(shared.LogLevelInfo, "All expected file changes received: file_a=%d, file_b=%d", a, b)
	s.handle.SignalInitComplete()
	sdk.Log(shared.LogLevelInfo, "Bootstrap file watcher test completed successfully!")
}
