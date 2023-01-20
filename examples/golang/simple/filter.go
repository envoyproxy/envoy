package main

import (
	"fmt"
	"strconv"

	"github.com/envoyproxy/envoy/contrib/golang/filters/http/source/go/pkg/api"
)

var LOCALREPLYFORBIDDENBODY = "localreply forbidden by encodedata"

type filter struct {
	callbacks api.FilterCallbackHandler
	path      string
}

func (f *filter) sendLocalReplyForbidden() api.StatusType {
	headers := make(map[string]string)
	body := fmt.Sprintf("forbidden from go, path: %s\r\n", f.path)
	f.callbacks.SendLocalReply(403, body, headers, -1, "test-from-go")
	return api.LocalReply
}

func (f *filter) DecodeHeaders(header api.RequestHeaderMap, endStream bool) api.StatusType {
	f.path, _ = header.Get(":path")
	header.Set("rsp-header-from-go", "foo-test")
	if f.path == "/forbidden" {
		return f.sendLocalReplyForbidden()
	}
	return api.Continue
}

func (f *filter) DecodeData(buffer api.BufferInstance, endStream bool) api.StatusType {
	println("enter decodedata")
	return api.Continue
}

func (f *filter) DecodeTrailers(trailers api.RequestTrailerMap) api.StatusType {
	return api.Continue
}

func (f *filter) EncodeHeaders(header api.ResponseHeaderMap, endStream bool) api.StatusType {
	if f.path == "/localreply/forbidden" {
		header.Set("Content-Length", strconv.Itoa(len(LOCALREPLYFORBIDDENBODY)))
	}
	header.Set("Rsp-Header-From-Go", "bar-test")
	return api.Continue
}

func (f *filter) EncodeData(buffer api.BufferInstance, endStream bool) api.StatusType {
	if f.path == "/localreply/forbidden" {
		if endStream {
			buffer.SetString(LOCALREPLYFORBIDDENBODY)
		} else {
			// TODO implement buffer->Drain, buffer.SetString means buffer->Drain(buffer.Len())
			buffer.SetString("")
		}
	}
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
