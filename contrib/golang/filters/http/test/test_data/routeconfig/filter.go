package main

import (
	"github.com/envoyproxy/envoy/contrib/golang/common/go/api"
)

type filter struct {
	api.PassThroughStreamFilter

	config    *config
	callbacks api.FilterCallbackHandler
}

func (f *filter) EncodeHeaders(header api.ResponseHeaderMap, endStream bool) api.StatusType {
	if f.config.removeHeader != "" {
		header.Del(f.config.removeHeader)
	}
	if f.config.setHeader != "" {
		header.Set(f.config.setHeader, "test-value")
	}
	return api.Continue
}
