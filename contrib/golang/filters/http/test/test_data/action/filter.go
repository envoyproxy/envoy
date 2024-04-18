package main

import (
	"net/url"
	"strings"

	"github.com/envoyproxy/envoy/contrib/golang/common/go/api"
)

type filter struct {
	api.PassThroughStreamFilter

	callbacks    api.FilterCallbackHandler
	query_params url.Values
}

func parseQuery(path string) url.Values {
	if idx := strings.Index(path, "?"); idx >= 0 {
		query := path[idx+1:]
		values, _ := url.ParseQuery(query)
		return values
	}
	return make(url.Values)
}

func getStatus(status string) api.StatusType {
	switch status {
	case "StopAndBuffer":
		return api.StopAndBuffer
	case "StopAndBufferWatermark":
		return api.StopAndBufferWatermark
	}
	return api.Continue
}

func (f *filter) DecodeHeaders(header api.RequestHeaderMap, endStream bool) api.StatusType {
	f.query_params = parseQuery(header.Path())

	decodeHeadersRet := f.query_params.Get("decodeHeadersRet")
	async := f.query_params.Get("async")
	if decodeHeadersRet != "" {
		if async != "" {
			go func() {
				defer f.callbacks.RecoverPanic()
				f.callbacks.Continue(getStatus(decodeHeadersRet))
			}()
			return api.Running
		}

		return getStatus(decodeHeadersRet)
	}

	return api.Continue
}

func (f *filter) EncodeHeaders(header api.ResponseHeaderMap, endStream bool) api.StatusType {
	encodeHeadersRet := f.query_params.Get("encodeHeadersRet")
	async := f.query_params.Get("async")
	if encodeHeadersRet != "" {
		if async != "" {
			go func() {
				defer f.callbacks.RecoverPanic()
				f.callbacks.Continue(getStatus(encodeHeadersRet))
			}()
			return api.Running
		}

		return getStatus(encodeHeadersRet)
	}

	return api.Continue
}
