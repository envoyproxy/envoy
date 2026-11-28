// Package scheduler manages main-thread tasks for bootstrap and cluster lifecycle hooks.
package scheduler

import "sync"

// Scheduler owns one native scheduler and the tasks it has posted to Envoy.
type Scheduler struct {
	mu      sync.Mutex
	commit  func(uint64)
	destroy func()
	tasks   map[uint64]func()
	nextID  uint64
	active  uint64
	closed  bool
}

// Init creates the native scheduler on first use. Closing an unused scheduler prevents
// later calls from touching the destroyed host object.
func (s *Scheduler) Init(create func() (commit func(uint64), destroy func())) {
	s.mu.Lock()
	defer s.mu.Unlock()
	if s.commit == nil && !s.closed {
		s.commit, s.destroy = create()
		s.tasks = make(map[uint64]func())
	}
}

func (s *Scheduler) Schedule(task func()) {
	s.mu.Lock()
	if s.tasks == nil {
		s.mu.Unlock()
		return
	}
	id := s.nextID
	s.nextID++
	s.tasks[id] = task
	s.active++
	s.mu.Unlock()

	// Close can run on the main thread while a commit is in flight. Keep the native
	// scheduler alive until every in-flight commit returns.
	defer func() {
		s.mu.Lock()
		s.active--
		destroy := s.closed && s.active == 0
		s.mu.Unlock()
		if destroy {
			s.destroy()
		}
	}()
	s.commit(id)
}

// Run dispatches a posted task on the main thread. The task may itself schedule more work.
func (s *Scheduler) Run(id uint64) {
	s.mu.Lock()
	task := s.tasks[id]
	delete(s.tasks, id)
	s.mu.Unlock()
	if task != nil {
		task()
	}
}

// Close discards pending tasks and releases the native scheduler after its last commit.
func (s *Scheduler) Close() {
	s.mu.Lock()
	destroy := !s.closed && s.active == 0 && s.destroy != nil
	s.closed = true
	s.tasks = nil
	s.mu.Unlock()
	if destroy {
		s.destroy()
	}
}
