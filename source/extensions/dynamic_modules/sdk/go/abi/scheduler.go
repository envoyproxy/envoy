package abi

/*
#cgo darwin LDFLAGS: -Wl,-undefined,dynamic_lookup
#cgo linux LDFLAGS: -Wl,--unresolved-symbols=ignore-all
#include <stdint.h>
*/
import "C"
import (
	"sync"
	"unsafe"
)

// dymScheduler is the SDK-side implementation of shared.Scheduler used by every extension
// surface that exposes a NewScheduler method (HTTP filter, network filter, cluster, bootstrap,
// etc.).
type dymScheduler struct {
	schedulerPtr  unsafe.Pointer
	schedulerLock sync.Mutex
	nextTaskID    uint64
	tasks         map[uint64]func()
	commitFunc    func(unsafe.Pointer, C.uint64_t)
	deleteFunc func(unsafe.Pointer)
}

func newDymScheduler(
	schedulerPtr unsafe.Pointer,
	commitFunc func(unsafe.Pointer, C.uint64_t),
	deleteFunc func(unsafe.Pointer),
) *dymScheduler {
	return &dymScheduler{
		schedulerPtr: schedulerPtr,
		tasks:        make(map[uint64]func()),
		commitFunc:   commitFunc,
		deleteFunc:   deleteFunc,
	}
}

// close synchronously frees the host-side scheduler. Idempotent: subsequent calls and
// the runtime finalizer both no-op once schedulerPtr is nil. Must be called from the
// extension's destroy hook so the host allocation is reclaimed before the .so unloads.
func (s *dymScheduler) close() {
	s.schedulerLock.Lock()
	ptr := s.schedulerPtr
	s.schedulerPtr = nil
	s.schedulerLock.Unlock()
	if ptr != nil && s.deleteFunc != nil {
		s.deleteFunc(ptr)
	}
}

func (s *dymScheduler) Schedule(task func()) {
	s.schedulerLock.Lock()
	taskID := s.nextTaskID
	s.nextTaskID++
	s.tasks[taskID] = task
	s.schedulerLock.Unlock()
	s.commitFunc(s.schedulerPtr, C.uint64_t(taskID))
}

func (s *dymScheduler) onScheduled(taskID uint64) {
	s.schedulerLock.Lock()
	task := s.tasks[taskID]
	delete(s.tasks, taskID)
	s.schedulerLock.Unlock()
	if task != nil {
		task()
	}
}
