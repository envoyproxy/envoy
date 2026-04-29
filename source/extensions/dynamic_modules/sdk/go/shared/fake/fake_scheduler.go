package fake

import (
	"sync"

	"github.com/envoyproxy/envoy/source/extensions/dynamic_modules/sdk/go/shared"
)

var _ shared.Scheduler = (*FakeScheduler)(nil)

// FakeScheduler is a deterministic in-memory implementation of shared.Scheduler for tests.
//
// Unlike a generated mock, FakeScheduler captures every function passed to Schedule and
// gives the test explicit control over when (or whether) those functions actually run. This
// makes it well-suited to verifying code paths that wait on async work without adding real
// concurrency to the test.
//
// Typical usage:
//
//	s := fake.NewFakeScheduler()
//	myFilter.handleAsync(s)         // module schedules work
//	if got := s.Pending(); got != 1 { ... }
//	s.RunAll()                       // drain in test thread
//
// FakeScheduler is safe for concurrent calls to Schedule from multiple goroutines, but the
// drain helpers (RunOne / RunAll) are intended to be called from a single test goroutine.
type FakeScheduler struct {
	mu      sync.Mutex
	pending []func()
}

// NewFakeScheduler creates a new FakeScheduler with no pending tasks.
func NewFakeScheduler() *FakeScheduler {
	return &FakeScheduler{}
}

// Schedule records the given function for later execution.
func (s *FakeScheduler) Schedule(fn func()) {
	if fn == nil {
		return
	}
	s.mu.Lock()
	s.pending = append(s.pending, fn)
	s.mu.Unlock()
}

// Pending returns the number of functions queued via Schedule that have not yet been run.
func (s *FakeScheduler) Pending() int {
	s.mu.Lock()
	defer s.mu.Unlock()
	return len(s.pending)
}

// RunOne pops and runs the oldest pending function. Returns true if a function was run, or
// false if the queue was empty. The function is run with the scheduler unlocked, so a
// scheduled function may itself call Schedule.
func (s *FakeScheduler) RunOne() bool {
	s.mu.Lock()
	if len(s.pending) == 0 {
		s.mu.Unlock()
		return false
	}
	fn := s.pending[0]
	s.pending = s.pending[1:]
	s.mu.Unlock()
	fn()
	return true
}

// RunAll runs every pending function in order. Functions scheduled by other functions during
// the drain are also run, until the queue is empty. Returns the total number of functions
// that ran.
func (s *FakeScheduler) RunAll() int {
	count := 0
	for s.RunOne() {
		count++
	}
	return count
}

// Reset clears all pending functions without running them.
func (s *FakeScheduler) Reset() {
	s.mu.Lock()
	s.pending = nil
	s.mu.Unlock()
}
