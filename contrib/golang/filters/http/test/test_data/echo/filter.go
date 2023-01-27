package main

import (
	"fmt"

	"github.com/envoyproxy/envoy/contrib/golang/filters/http/source/go/pkg/api"
)

type filter struct {
	callbacks api.FilterCallbackHandler
	path      string
}

func (f *filter) sendLocalReply() api.StatusType {
	headers := make(map[string]string)
	body := fmt.Sprintf("forbidden from go, path: %s\r\n", f.path)
	f.callbacks.SendLocalReply(403, body, headers, -1, "test-from-go")
	return api.LocalReply
}

func (f *filter) DecodeHeaders(header api.RequestHeaderMap, endStream bool) api.StatusType {
	header.Del("x-test-header-1")
	f.path, _ = header.Get(":path")
	header.Set("rsp-header-from-go", "foo-test")
	if f.path == "/localreply" {
		return f.sendLocalReply()
	}
	return api.Continue
}

func (f *filter) DecodeData(buffer api.BufferInstance, endStream bool) api.StatusType {
	return api.Continue
}

func (f *filter) DecodeTrailers(trailers api.RequestTrailerMap) api.StatusType {
	return api.Continue
}

func (f *filter) EncodeHeaders(header api.ResponseHeaderMap, endStream bool) api.StatusType {
	header.Set("Rsp-Header-From-Go", "bar-test")
	return api.Continue
}

func (f *filter) EncodeData(buffer api.BufferInstance, endStream bool) api.StatusType {
	return api.Continue
}

func (f *filter) EncodeTrailers(trailers api.ResponseTrailerMap) api.StatusType {
	return api.Continue
}

func (f *filter) OnDestroy(reason api.DestroyReason) {
}

func (f *filter) Callbacks() api.FilterCallbacks {
	return f.callbacks
}
