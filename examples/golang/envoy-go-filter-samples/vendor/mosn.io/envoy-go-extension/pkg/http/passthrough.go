package http

import (
	"mosn.io/envoy-go-extension/pkg/http/api"
)

type passThroughFilter struct {
	callbacks api.FilterCallbackHandler
}

func (f *passThroughFilter) DecodeHeaders(header api.RequestHeaderMap, endStream bool) api.StatusType {
	return api.Continue
}

func (f *passThroughFilter) DecodeData(buffer api.BufferInstance, endStream bool) api.StatusType {
	return api.Continue
}

func (f *passThroughFilter) DecodeTrailers(trailers api.RequestTrailerMap) api.StatusType {
	return api.Continue
}

func (f *passThroughFilter) EncodeHeaders(header api.ResponseHeaderMap, endStream bool) api.StatusType {
	return api.Continue
}

func (f *passThroughFilter) EncodeData(buffer api.BufferInstance, endStream bool) api.StatusType {
	return api.Continue
}

func (f *passThroughFilter) EncodeTrailers(trailers api.ResponseTrailerMap) api.StatusType {
	return api.Continue
}

func (f *passThroughFilter) OnDestroy(reason api.DestroyReason) {
	// fmt.Printf("OnDestory, reason: %d\n", reason)
}

func (f *passThroughFilter) Callbacks() api.FilterCallbacks {
	return f.callbacks
}

func PassThroughFactory(interface{}) api.HttpFilterFactory {
	return func(callbacks api.FilterCallbackHandler) api.HttpFilter {
		return &passThroughFilter{
			callbacks: callbacks,
		}
	}
}
