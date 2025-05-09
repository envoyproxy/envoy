package add_data

import (
	"net/url"
	"strconv"

	"github.com/envoyproxy/envoy/contrib/golang/common/go/api"
)

type filter struct {
	api.PassThroughStreamFilter

	callbacks api.FilterCallbackHandler
	config    *config
	params    url.Values
}

func (f *filter) DecodeHeaders(headers api.RequestHeaderMap, endStream bool) api.StatusType {
	path := headers.Path()
	u, _ := url.Parse(path)
	f.params = u.Query()

	if f.params.Has("calledInDecodeHeaders") {
		headers.Set(":method", "POST")
		headers.Set("content-length", strconv.Itoa(len(f.params.Get("calledInDecodeHeaders"))))
		f.callbacks.DecoderFilterCallbacks().AddData([]byte(f.params.Get("calledInDecodeHeaders")), true)
	}

	return api.Continue
}

func (f *filter) callNotAllowed(data string, buffer api.BufferInstance) {
	defer func() {
		if p := recover(); p != nil {
			buffer.Append([]byte(" called in DecodeData is not allowed"))
		}
	}()
	f.callbacks.DecoderFilterCallbacks().AddData([]byte(data), true)
}

func (f *filter) DecodeData(buffer api.BufferInstance, endStream bool) api.StatusType {
	data := f.params.Get("calledInDecodeData")
	if len(data) > 0 && endStream {
		f.callNotAllowed(data, buffer)
	}

	if f.params.Has("bufferAllData") {
		return api.StopAndBuffer
	}
	return api.Continue
}

func (f *filter) DecodeTrailers(trailers api.RequestTrailerMap) api.StatusType {
	if f.params.Has("calledInDecodeTrailers") {
		streaming := !f.params.Has("bufferAllData")
		f.callbacks.DecoderFilterCallbacks().AddData([]byte(f.params.Get("calledInDecodeTrailers")), streaming)
	}
	return api.Continue
}

func (f *filter) EncodeHeaders(headers api.ResponseHeaderMap, endStream bool) api.StatusType {
	if f.params.Has("calledInEncodeHeaders") {
		// Test both sync and async paths
		go func() {
			headers.Set("content-length", strconv.Itoa(len(f.params.Get("calledInEncodeHeaders"))))
			f.callbacks.EncoderFilterCallbacks().AddData([]byte(f.params.Get("calledInEncodeHeaders")), true)
			f.callbacks.EncoderFilterCallbacks().Continue(api.Continue)
		}()
		return api.Running
	}

	return api.Continue
}

func (f *filter) EncodeData(buffer api.BufferInstance, endStream bool) api.StatusType {
	if f.params.Has("bufferAllData") {
		return api.StopAndBuffer
	}
	return api.Continue
}

func (f *filter) EncodeTrailers(trailers api.ResponseTrailerMap) api.StatusType {
	if f.params.Has("calledInEncodeTrailers") {
		go func() {
			streaming := !f.params.Has("bufferAllData")
			f.callbacks.EncoderFilterCallbacks().AddData([]byte(f.params.Get("calledInEncodeTrailers")), streaming)
			f.callbacks.EncoderFilterCallbacks().Continue(api.Continue)
		}()
		return api.Running
	}
	return api.Continue
}
