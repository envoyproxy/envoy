package bufferinjectdata

import (
	"net/url"
	"runtime/debug"
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

func (f *filter) disallowInjectData() {
	defer func() {
		if p := recover(); p != nil {
			api.LogErrorf("panic: %v\n%s", p, debug.Stack())
			f.callbacks.DecoderFilterCallbacks().SendLocalReply(400, "Not allowed", nil, 0, "")
		}
	}()
	f.callbacks.DecoderFilterCallbacks().InjectData([]byte("just try"))
}

func (f *filter) DecodeHeaders(headers api.RequestHeaderMap, endStream bool) api.StatusType {
	path := headers.Path()
	u, _ := url.Parse(path)
	f.params = u.Query()

	if f.params.Has("inject_data_when_processing_header") {
		f.disallowInjectData()
		return api.LocalReply
	}

	if f.params.Has("bufferingly_decode") {
		return api.StopAndBuffer
	}
	return api.Continue
}

func (f *filter) DecodeData(buffer api.BufferInstance, endStream bool) api.StatusType {
	if f.params.Has("inject_data_when_processing_data_synchronously") {
		f.disallowInjectData()
		return api.LocalReply
	}

	// buffer.InjectData must be called in async mode
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
	cb := f.callbacks.DecoderFilterCallbacks()
	if f.params.Has("nonbufferingly_decode") {
		return f.processDataNonbufferingly(cb, buffer, endStream)
	} else if f.params.Has("bufferingly_decode") {
		return f.processDataBufferingly(cb, buffer, endStream)
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
	// buffer.InjectData must be called in async mode
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
	cb := f.callbacks.EncoderFilterCallbacks()
	if f.params.Has("nonbufferingly_encode") {
		return f.processDataNonbufferingly(cb, buffer, endStream)
	}
	if f.params.Has("bufferingly_encode") {
		return f.processDataBufferingly(cb, buffer, endStream)
	}
	return api.Continue
}

func (f *filter) processDataNonbufferingly(cb api.FilterProcessCallbacks, buffer api.BufferInstance, endStream bool) api.StatusType {
	f.flushInNonbufferedResponse(cb, buffer)
	f.count++
	return api.Continue
}

func injectData(cb api.FilterProcessCallbacks, data string, wait bool) {
	cb.InjectData([]byte(data))
}

func (f *filter) flushInNonbufferedResponse(cb api.FilterProcessCallbacks, buffer api.BufferInstance) {
	// The remote sends: "To be, " and then "that is "
	api.LogInfof("The remote sends %s", buffer.String())
	cb.InjectData(buffer.Bytes())
	buffer.Reset()
	if f.count == 0 {
		injectData(cb, "or not to be, ", false)
	} else if f.count == 1 {
		injectData(cb, "the question", false)
	}
}

func (f *filter) processDataBufferingly(cb api.FilterProcessCallbacks, buffer api.BufferInstance, endStream bool) api.StatusType {
	if !endStream {
		return api.StopAndBuffer
	}

	var wg sync.WaitGroup
	wg.Add(1)
	go func(buffer api.BufferInstance) {
		defer wg.Done()
		flushInBufferedResponse(cb, buffer)
	}(buffer)
	wg.Wait()
	return api.Continue
}

func flushInBufferedResponse(cb api.FilterProcessCallbacks, buffer api.BufferInstance) {
	// The remote sends: "To be, "
	api.LogInfof("The remote sends %s", buffer.String())
	cb.InjectData(buffer.Bytes())
	buffer.Reset()
	injectData(cb, "or not to be, ", false)
	time.Sleep(10 * time.Millisecond)
	injectData(cb, "that is the question", false)
}
