package main

import (
	"strconv"
	"time"

	"github.com/envoyproxy/envoy/contrib/golang/common/go/api"
)

type filter struct {
	api.PassThroughStreamFilter

	callbacks api.FilterCallbackHandler
	path      string
	config    *config

	failed bool
}

func (f *filter) assertProperty(name, exp string) {
	act, err := f.callbacks.GetProperty(name)
	if err != nil {
		act = err.Error()
	}
	if exp != act {
		f.callbacks.Log(api.Critical, name+" expect "+exp+" got "+act)
		f.failed = true
	}
}

func (f *filter) panicIfFailed() {
	if f.failed {
		panic("Check the critical log for the failed cases")
	}
}

func (f *filter) DecodeHeaders(header api.RequestHeaderMap, endStream bool) api.StatusType {
	ts, _ := f.callbacks.GetProperty("request.time")
	ymd := ts[:len("2023-07-31T00:00:00")]
	startTime, _ := time.Parse("2006-01-02T15:04:05", ymd)
	if time.Now().UTC().Sub(startTime) > 1*time.Minute {
		f.callbacks.Log(api.Critical, "got request.time "+ts)
		f.failed = true
	}

	f.assertProperty("request.protocol", "HTTP/1.1")
	f.assertProperty("request.path", "/property?a=1")
	f.assertProperty("request.url_path", "/property")
	f.assertProperty("request.query", "a=1")
	f.assertProperty("request.host", "test.com")
	f.assertProperty("request.scheme", "http")
	f.assertProperty("request.method", "POST")
	f.assertProperty("request.referer", "r")
	f.assertProperty("request.useragent", "ua")
	f.assertProperty("request.id", "xri")

	f.assertProperty("request.duration", api.ErrValueNotFound.Error()) // available only when the request is finished

	f.assertProperty("source.address", f.callbacks.StreamInfo().DownstreamRemoteAddress())
	f.assertProperty("destination.address", f.callbacks.StreamInfo().DownstreamLocalAddress())
	f.assertProperty("connection.mtls", "false")
	// route name can be determinated in the decode phase
	f.assertProperty("xds.route_name", "test-route-name")

	// non-existed attribute
	f.assertProperty("request.user_agent", api.ErrValueNotFound.Error())

	// access response attribute in the decode phase
	f.assertProperty("response.total_size", "0")

	// bad case
	// strange input
	for _, attr := range []string{
		".",
		".total_size",
	} {
		f.assertProperty(attr, api.ErrValueNotFound.Error())
	}
	// unsupported value type
	for _, attr := range []string{
		// unknown type
		"",
		// map type
		"request",
		"request.",
	} {
		f.assertProperty(attr, api.ErrSerializationFailure.Error())
	}

	// error handling
	_, err := f.callbacks.GetProperty(".not_found")
	if err != api.ErrValueNotFound {
		f.callbacks.Log(api.Critical, "unexpected error "+err.Error())
		f.failed = true
	}
	return api.Continue
}

func (f *filter) EncodeHeaders(header api.ResponseHeaderMap, endStream bool) api.StatusType {
	f.assertProperty("xds.route_name", "test-route-name")
	f.assertProperty("xds.cluster_name", "cluster_0")
	f.assertProperty("xds.cluster_metadata", "")

	code, _ := f.callbacks.StreamInfo().ResponseCode()
	exp := ""
	if code != 0 {
		exp = strconv.Itoa(int(code))
	}
	f.assertProperty("response.code", exp)
	f.assertProperty("response.code_details", "via_upstream")

	f.assertProperty("request.size", "10") // "helloworld"
	size, _ := f.callbacks.GetProperty("request.total_size")
	intSize, _ := strconv.Atoi(size)
	if intSize <= 10 {
		f.callbacks.Log(api.Critical, "got request.total_size "+size)
		f.failed = true
	}
	f.assertProperty("request.referer", "r")

	return api.Continue
}

func (f *filter) EncodeData(buffer api.BufferInstance, endStream bool) api.StatusType {
	f.assertProperty("response.code", "200")

	// panic if any condition is not met
	f.panicIfFailed()
	return api.Continue
}

func (f *filter) OnLog(reqHeader api.RequestHeaderMap, reqTrailer api.RequestTrailerMap, respHeader api.ResponseHeaderMap, respTrailer api.ResponseTrailerMap) {
	f.assertProperty("response.size", "7") // "goodbye"

	// panic if any condition is not met
	f.panicIfFailed()
}
