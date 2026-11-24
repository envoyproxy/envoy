package scheduler

import (
	"sync"
	"testing"
	"time"
)

func waitFor(t *testing.T, event <-chan struct{}, description string) {
	t.Helper()
	select {
	case <-event:
	case <-time.After(10 * time.Second):
		t.Fatalf("timed out waiting for %s", description)
	}
}

func TestCloseDuringCommits(t *testing.T) {
	var s Scheduler
	var deletes int
	firstCommit := make(chan struct{})
	release := make(chan struct{}, 1)
	defer close(release)
	s.Init(func() (func(uint64), func()) {
		commit := func(id uint64) {
			switch id {
			case 0:
				close(firstCommit)
				<-release
			case 1:
				// The last native owner releases its reference inside this commit.
				s.Close()
			default:
				t.Errorf("unexpected commit %d", id)
			}
		}
		return commit, func() { deletes++ }
	})

	discarded := func() { t.Error("ran a task after close") }
	firstDone := make(chan struct{})
	go func() {
		s.Schedule(discarded)
		close(firstDone)
	}()
	waitFor(t, firstCommit, "the first commit to enter the host")
	secondDone := make(chan struct{})
	go func() {
		s.Schedule(discarded)
		close(secondDone)
	}()
	waitFor(t, secondDone, "the reentrant close to return")
	if deletes != 0 {
		t.Error("deleted the scheduler while the first commit was active")
	}
	s.Schedule(discarded)
	s.Run(0)
	s.Run(1)
	release <- struct{}{}
	waitFor(t, firstDone, "the first commit to finish")
	s.Close()
	if deletes != 1 {
		t.Fatalf("delete called %d times, want 1", deletes)
	}
}

func TestConcurrentInitAndDispatch(t *testing.T) {
	var s Scheduler
	const count = 10
	posted := make(chan uint64, count)
	var ran [count]int
	creates, deletes := 0, 0
	var wg sync.WaitGroup
	for i := range count {
		wg.Go(func() {
			s.Init(func() (func(uint64), func()) {
				creates++
				return func(id uint64) { posted <- id }, func() { deletes++ }
			})
			s.Schedule(func() { ran[i]++ })
		})
	}
	wg.Wait()
	if got := len(posted); got != count {
		t.Fatalf("committed %d tasks, want %d", got, count)
	}
	for range count {
		id := <-posted
		s.Run(id)
		s.Run(id)
	}
	for i := range ran {
		if ran[i] != 1 {
			t.Errorf("task %d ran %d times, want 1", i, ran[i])
		}
	}
	// The scheduler lock must be released before the task runs.
	s.Schedule(s.Close)
	done := make(chan struct{})
	go func() {
		s.Run(<-posted)
		close(done)
	}()
	waitFor(t, done, "the task to close its scheduler")
	if creates != 1 || deletes != 1 {
		t.Errorf("create and delete called %d and %d times, want 1 each", creates, deletes)
	}
}

func TestCloseBeforeInit(t *testing.T) {
	var s Scheduler
	s.Close()
	s.Init(func() (func(uint64), func()) {
		t.Fatal("created native scheduler after close")
		return nil, nil
	})
}
