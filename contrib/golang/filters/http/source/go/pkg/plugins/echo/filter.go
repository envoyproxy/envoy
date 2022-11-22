package echo

import (
    "fmt"

    "github.com/envoyproxy/envoy/contrib/golang/filters/http/source/go/pkg/api"
)

type filter struct {
    callbacks api.FilterCallbackHandler
    path      string
}

func (f *filter) sendLocalReply() api.StatusType {
    // TODO: local reply headers
    headers := make(map[string]string)
    body := fmt.Sprintf("echo from go, path: %s\r\n", f.path)
    f.callbacks.SendLocalReply(200, body, headers, -1, "echo-from-go")
    return api.LocalReply
}

func (f *filter) DecodeHeaders(header api.RequestHeaderMap, endStream bool) api.StatusType {
    return f.sendLocalReply()
}

func (f *filter) DecodeData(buffer api.BufferInstance, endStream bool) api.StatusType {
    return api.Continue
}

func (f *filter) DecodeTrailers(trailers api.RequestTrailerMap) api.StatusType {
    return api.Continue
}

func (f *filter) EncodeHeaders(header api.ResponseHeaderMap, endStream bool) api.StatusType {
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
