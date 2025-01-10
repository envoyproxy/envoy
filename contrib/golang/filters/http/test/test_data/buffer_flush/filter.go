package main

import (
	"net/url"
	"sync"
	"time"

	"github.com/envoyproxy/envoy/contrib/golang/common/go/api"
)

type filter struct {
	api.PassThroughStreamFilter

	callbacks api.FilterCallbackHandler
	params    url.Values
	config    *config

	count int
}

func (f *filter) DecodeHeaders(headers api.RequestHeaderMap, endStream bool) api.StatusType {
	path := headers.Path()
	u, _ := url.Parse(path)
	f.params = u.Query()

	if f.params.Has("bufferingly_decode") {
		return api.StopAndBuffer
	}
	return api.Continue
}

func (f *filter) DecodeData(buffer api.BufferInstance, endStream bool) api.StatusType {
	// buffer.Flush must be called in async mode
	go func() {
		defer f.callbacks.DecoderFilterCallbacks().RecoverPanic()

		status := f.decodeData(buffer, endStream)
		if status != api.LocalReply {
			f.callbacks.DecoderFilterCallbacks().Continue(status)
		}
	}()
	return api.Running
}

func (f *filter) decodeData(buffer api.BufferInstance, endStream bool) api.StatusType {
	if f.params.Has("nonbufferingly_decode") {
		return f.processDataNonbufferingly(buffer, endStream)
	} else if f.params.Has("bufferingly_decode") {
		return f.processDataBufferingly(buffer, endStream)
	}
	return api.Continue
}

func (f *filter) EncodeHeaders(headers api.ResponseHeaderMap, endStream bool) api.StatusType {
	if f.params.Has("bufferingly_encode") {
		return api.StopAndBuffer
	}
	return api.Continue
}

func (f *filter) EncodeData(buffer api.BufferInstance, endStream bool) api.StatusType {
	// buffer.Flush must be called in async mode
	go func() {
		defer f.callbacks.EncoderFilterCallbacks().RecoverPanic()

		status := f.encodeData(buffer, endStream)
		if status != api.LocalReply {
			f.callbacks.EncoderFilterCallbacks().Continue(status)
		}
	}()
	return api.Running
}

func (f *filter) encodeData(buffer api.BufferInstance, endStream bool) api.StatusType {
	if f.params.Has("nonbufferingly_encode") {
		return f.processDataNonbufferingly(buffer, endStream)
	}
	if f.params.Has("bufferingly_encode") {
		return f.processDataBufferingly(buffer, endStream)
	}
	return api.Continue
}

func (f *filter) processDataNonbufferingly(buffer api.BufferInstance, endStream bool) api.StatusType {
	f.flushInNonbufferedResponse(buffer)
	f.count++
	return api.Continue
}

func injectData(b api.BufferInstance, data string, wait bool) {
	b.SetString(data)
	b.Flush(wait)
}

func (f *filter) flushInNonbufferedResponse(buffer api.BufferInstance) {
	// The remote sends: "To be, " and then "that is "
	api.LogInfof("The remote sends %s", buffer.String())
	buffer.Flush(false)
	if f.count == 0 {
		injectData(buffer, "or not to be, ", false)
	} else if f.count == 1 {
		injectData(buffer, "the question", false)
	}
}

func (f *filter) processDataBufferingly(buffer api.BufferInstance, endStream bool) api.StatusType {
	if !endStream {
		return api.StopAndBuffer
	}

	var wg sync.WaitGroup
	wg.Add(1)
	go func(buffer api.BufferInstance) {
		defer wg.Done()
		flushInBufferedResponse(buffer)
	}(buffer)
	wg.Wait()
	return api.Continue
}

func flushInBufferedResponse(buffer api.BufferInstance) {
	// The remote sends: "To be, "
	api.LogInfof("The remote sends %s", buffer.String())
	buffer.Flush(false)
	injectData(buffer, "or not to be, ", false)
	time.Sleep(10 * time.Millisecond)
	injectData(buffer, "that is the question", false)
}
