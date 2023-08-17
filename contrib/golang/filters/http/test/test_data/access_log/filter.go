package main

import (
	"strconv"
	"sync"
	"time"

	"github.com/envoyproxy/envoy/contrib/golang/common/go/api"
)

var (
	counter = 0
	wg      = &sync.WaitGroup{}

	respCode      string
	canRunAsyncly bool
)

type filter struct {
	api.PassThroughStreamFilter

	callbacks api.FilterCallbackHandler
	config    *config
}

func (f *filter) DecodeHeaders(header api.RequestHeaderMap, endStream bool) api.StatusType {
	if counter > 0 {
		wg.Wait()
		header.Set("respCode", respCode)
		header.Set("canRunAsyncly", strconv.FormatBool(canRunAsyncly))
	}

	return api.Continue
}

func (f *filter) OnLog() {
	code, _ := f.callbacks.StreamInfo().ResponseCode()
	respCode = strconv.Itoa(int(code))
	api.LogCritical(respCode)

	wg.Add(1)
	go func() {
		time.Sleep(1 * time.Millisecond)
		canRunAsyncly = true
		wg.Done()
	}()

	counter++
}
