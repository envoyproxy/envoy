package main

import (
	"strconv"
	"strings"
	"sync"
	"time"

	"github.com/envoyproxy/envoy/contrib/golang/common/go/api"
)

var (
	counter = 0
	wg      = &sync.WaitGroup{}

	respCode      string
	respSize      string
	canRunAsyncly bool

	canRunAsynclyForDownstreamStart bool

	canRunAsynclyForDownstreamPeriodic bool

	referers = []string{}
)

type filter struct {
	api.PassThroughStreamFilter

	callbacks api.FilterCallbackHandler
	config    *config
}

func (f *filter) DecodeHeaders(header api.RequestHeaderMap, endStream bool) api.StatusType {
	if counter == 0 {
		query, _ := f.callbacks.GetProperty("request.query")
		if query == "periodic=1" {
			go func() {
				defer f.callbacks.RecoverPanic()

				// trigger AccessLogDownstreamPeriodic
				time.Sleep(110 * time.Millisecond)
				f.callbacks.Continue(api.Continue)
			}()
			return api.Running
		}
	}

	if counter > 0 {
		wg.Wait()
		header.Set("respCode", respCode)
		header.Set("respSize", respSize)
		header.Set("canRunAsyncly", strconv.FormatBool(canRunAsyncly))
		header.Set("canRunAsynclyForDownstreamStart", strconv.FormatBool(canRunAsynclyForDownstreamStart))
		header.Set("canRunAsynclyForDownstreamPeriodic", strconv.FormatBool(canRunAsynclyForDownstreamPeriodic))

		header.Set("referers", strings.Join(referers, ";"))

		// reset for the next test
		referers = []string{}
		// the counter will be 0 when this request is ended
		counter = -1

	}

	return api.Continue
}

func (f *filter) OnLogDownstreamStart() {
	referer, err := f.callbacks.GetProperty("request.referer")
	if err != nil {
		api.LogErrorf("err: %s", err)
		return
	}

	referers = append(referers, referer)

	wg.Add(1)
	go func() {
		time.Sleep(1 * time.Millisecond)
		canRunAsynclyForDownstreamStart = true
		wg.Done()
	}()
}

func (f *filter) OnLogDownstreamPeriodic() {
	referer, err := f.callbacks.GetProperty("request.referer")
	if err != nil {
		api.LogErrorf("err: %s", err)
		return
	}

	referers = append(referers, referer)

	wg.Add(1)
	go func() {
		time.Sleep(1 * time.Millisecond)
		canRunAsynclyForDownstreamPeriodic = true
		wg.Done()
	}()
}

func (f *filter) OnLog() {
	code, ok := f.callbacks.StreamInfo().ResponseCode()
	if !ok {
		return
	}
	respCode = strconv.Itoa(int(code))
	api.LogCritical(respCode)
	size, err := f.callbacks.GetProperty("response.size")
	if err != nil {
		api.LogErrorf("err: %s", err)
		return
	}
	respSize = size

	wg.Add(1)
	go func() {
		time.Sleep(1 * time.Millisecond)
		canRunAsyncly = true
		wg.Done()
	}()

	counter++
}
