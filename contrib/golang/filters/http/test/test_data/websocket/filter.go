package main

import (
	"fmt"

	"github.com/envoyproxy/envoy/contrib/golang/common/go/api"
)

type filter struct {
	api.PassThroughStreamFilter

	callbacks api.FilterCallbackHandler
}

func (f *filter) DecodeHeaders(header api.RequestHeaderMap, endStream bool) api.StatusType {
	header.Set("test-websocket-req-key", "foo")
	return api.Continue
}

func (f *filter) DecodeData(buffer api.BufferInstance, endStream bool) api.StatusType {
	f.callbacks.Log(api.Error, fmt.Sprintf("body: %s, end_stream: %v", buffer.String(), endStream))
	if !endStream && buffer.Len() != 0 {
		buffer.PrependString("Hello_")
	}
	return api.Continue
}

func (f *filter) EncodeHeaders(header api.ResponseHeaderMap, endStream bool) api.StatusType {
	header.Set("test-websocket-rsp-key", "bar")
	return api.Continue
}

func (f *filter) EncodeData(buffer api.BufferInstance, endStream bool) api.StatusType {
	f.callbacks.Log(api.Error, fmt.Sprintf("body: %s, end_stream: %v", buffer.String(), endStream))
	if !endStream && buffer.Len() != 0 {
		buffer.PrependString("Bye_")
	}
	return api.Continue
}
