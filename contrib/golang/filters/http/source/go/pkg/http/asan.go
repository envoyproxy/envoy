package http

import (
	"runtime"
	"sync"

	"github.com/envoyproxy/envoy/contrib/golang/common/go/api"
)

var (
	asanTestEnabled = false
)

// forceGCFinalizer enforce GC and wait GC finalizer finished.
// Just for testing, to make asan happy.
func forceGCFinalizer() {
	var wg sync.WaitGroup
	wg.Add(1)

	{
		// create a fake httpRequest to trigger GC finalizer.
		fake := &httpRequest{}
		runtime.SetFinalizer(fake, func(*httpRequest) {
			wg.Done()
		})
	}

	api.LogWarn("golang filter enforcing GC")
	// enforce a GC cycle.
	runtime.GC()

	// wait GC finalizers finished.
	wg.Wait()
}
