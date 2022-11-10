package simple

import (
	"fmt"

	"mosn.io/envoy-go-extension/pkg/http/api"
)

type filter struct {
	callbacks api.FilterCallbackHandler
	path      string
}

func (f *filter) sendLocalReply() api.StatusType {
	// TODO: local reply headers
	headers := make(map[string]string)
	body := fmt.Sprintf("forbidden from go, path: %s\r\n", f.path)
	f.callbacks.SendLocalReply(403, body, headers, -1, "test-from-go")
	return api.LocalReply
}

func (f *filter) DecodeHeaders(header api.RequestHeaderMap, endStream bool) api.StatusType {
	f.path = header.Get(":path")
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
	// fmt.Printf("OnDestory, reason: %d\n", reason)
}

func (f *filter) Callbacks() api.FilterCallbacks {
	return f.callbacks
}
