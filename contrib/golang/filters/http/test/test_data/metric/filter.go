package main

import (
	"net/url"
	"strconv"
	"strings"

	"github.com/envoyproxy/envoy/contrib/golang/common/go/api"
)

type filter struct {
	api.PassThroughStreamFilter

	callbacks    api.FilterCallbackHandler
	config       *config
	query_params url.Values
	path         string

	// test mode, from query parameters
	async bool
}

func parseQuery(path string) url.Values {
	if idx := strings.Index(path, "?"); idx >= 0 {
		query := path[idx+1:]
		values, _ := url.ParseQuery(query)
		return values
	}
	return make(url.Values)
}

func (f *filter) initRequest(header api.RequestHeaderMap) {
	f.path = header.Path()
	f.query_params = parseQuery(f.path)
	if f.query_params.Get("async") != "" {
		f.async = true
	}
}

func (f *filter) decodeHeaders(header api.RequestHeaderMap, endStream bool) api.StatusType {
	f.config.counter.Increment(2)
	value := f.config.counter.Get()
	header.Add("go-metric-counter-test-header-key", strconv.FormatUint(value, 10))

	f.config.counter.Record(1)
	value = f.config.counter.Get()
	header.Add("go-metric-counter-record-test-header-key", strconv.FormatUint(value, 10))

	f.config.gauge.Increment(3)
	value = f.config.gauge.Get()
	header.Add("go-metric-gauge-test-header-key", strconv.FormatUint(value, 10))

	f.config.gauge.Record(1)
	value = f.config.gauge.Get()
	header.Add("go-metric-gauge-record-test-header-key", strconv.FormatUint(value, 10))

	return api.Continue
}

func (f *filter) DecodeHeaders(header api.RequestHeaderMap, endStream bool) api.StatusType {
	f.initRequest(header)
	if f.async {
		go func() {
			defer f.callbacks.RecoverPanic()

			status := f.decodeHeaders(header, endStream)
			if status != api.LocalReply {
				f.callbacks.Continue(status)
			}
		}()
		return api.Running
	} else {
		status := f.decodeHeaders(header, endStream)
		return status
	}
}
