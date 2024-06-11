package main

import (
	"fmt"
	"runtime"

	"github.com/envoyproxy/envoy/contrib/golang/common/go/api"
)

type filter struct {
	api.PassThroughStreamFilter

	callbacks api.FilterCallbackHandler
	path      string
	config    *config
}

func (f *filter) sendLocalReply() api.StatusType {
	echoBody := f.config.echoBody
	{
		body := fmt.Sprintf("%s, path: %s\r\n", echoBody, f.path)
		f.callbacks.DecoderFilterCallbacks().SendLocalReply(403, body, nil, 0, "")
	}
	// Force GC to free the body string.
	// For the case that C++ shouldn't touch the memory of the body string,
	// after the sendLocalReply function returns.
	runtime.GC()
	return api.LocalReply
}

func (f *filter) DecodeHeaders(header api.RequestHeaderMap, endStream bool) api.StatusType {
	header.Del("x-test-header-1")
	f.path, _ = header.Get(":path")
	header.Set("rsp-header-from-go", "foo-test")
	// For the convenience of testing, it's better to in the config parse phase
	matchPath := f.config.matchPath
	if matchPath != "" && f.path == matchPath {
		return f.sendLocalReply()
	}
	return api.Continue
}

func (f *filter) EncodeHeaders(header api.ResponseHeaderMap, endStream bool) api.StatusType {
	header.Set("Rsp-Header-From-Go", "bar-test")
	return api.Continue
}
