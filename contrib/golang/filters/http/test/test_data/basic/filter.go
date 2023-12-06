package main

import (
	"fmt"
	"net/url"
	"strconv"
	"strings"
	"time"

	"github.com/envoyproxy/envoy/contrib/golang/common/go/api"
)

type filter struct {
	api.PassThroughStreamFilter

	callbacks       api.FilterCallbackHandler
	req_body_length uint64
	query_params    url.Values
	scheme          string
	method          string
	path            string
	host            string

	// for bad api call testing
	header api.RequestHeaderMap

	// test mode, from query parameters
	async       bool
	sleep       bool   // all sleep
	data_sleep  bool   // only sleep in data phase
	localreplay string // send local reply
	databuffer  string // return api.Stop
	panic       string // hit panic in which phase
	badapi      bool   // bad api call
}

func parseQuery(path string) url.Values {
	if idx := strings.Index(path, "?"); idx >= 0 {
		query := path[idx+1:]
		values, _ := url.ParseQuery(query)
		return values
	}
	return make(url.Values)
}

func badcode() {
	// panic index out of range
	s := []int{1}
	s[1] = s[5]
}

func (f *filter) initRequest(header api.RequestHeaderMap) {
	f.header = header

	f.req_body_length = 0

	f.scheme = header.Scheme()
	f.method = header.Method()
	f.path = header.Path()
	f.host = header.Host()

	f.query_params = parseQuery(f.path)
	if f.query_params.Get("async") != "" {
		f.async = true
	}
	if f.query_params.Get("sleep") != "" {
		f.sleep = true
	}
	if f.query_params.Get("data_sleep") != "" {
		f.data_sleep = true
	}
	if f.query_params.Get("decode_localrepaly") != "" {
		f.data_sleep = true
	}
	f.databuffer = f.query_params.Get("databuffer")
	f.localreplay = f.query_params.Get("localreply")
	f.panic = f.query_params.Get("panic")
	f.badapi = f.query_params.Get("badapi") != ""
}

func (f *filter) fail(msg string, a ...any) api.StatusType {
	body := fmt.Sprintf(msg, a...)
	f.callbacks.Log(api.Error, fmt.Sprintf("test failed: %s", body))
	f.callbacks.SendLocalReply(500, body, nil, 0, "")
	return api.LocalReply
}

func (f *filter) sendLocalReply(phase string) api.StatusType {
	headers := map[string]string{
		"Content-type": "text/html",
		"test-phase":   phase,
	}
	body := fmt.Sprintf("forbidden from go in %s\r\n", phase)
	f.callbacks.SendLocalReply(403, body, headers, 0, "")
	return api.LocalReply
}

// test: get, set, remove, values, add
func (f *filter) decodeHeaders(header api.RequestHeaderMap, endStream bool) api.StatusType {
	// test logging
	f.callbacks.Log(api.Trace, "log test")
	f.callbacks.Log(api.Debug, "log test")
	f.callbacks.Log(api.Info, "log test")
	f.callbacks.Log(api.Warn, "log test")
	f.callbacks.Log(api.Error, "log test")
	f.callbacks.Log(api.Critical, "log test")

	api.LogTrace("log test")
	api.LogDebug("log test")
	api.LogInfo("log test")
	api.LogWarn("log test")
	api.LogError("log test")
	api.LogCritical("log test")

	api.LogTracef("log test %v", endStream)
	api.LogDebugf("log test %v", endStream)
	api.LogInfof("log test %v", endStream)
	api.LogWarnf("log test %v", endStream)
	api.LogErrorf("log test %v", endStream)
	api.LogCriticalf("log test %v", endStream)

	if f.callbacks.LogLevel() != api.GetLogLevel() {
		return f.fail("log level mismatch")
	}

	if f.sleep {
		time.Sleep(time.Millisecond * 100) // sleep 100 ms
	}

	_, found := header.Get("x-set-metadata")
	if found {
		md := f.callbacks.StreamInfo().DynamicMetadata()
		empty_metadata := md.Get("filter.go")
		if len(empty_metadata) != 0 {
			return f.fail("Metadata should be empty")
		}
		md.Set("filter.go", "foo", "bar")
		metadata := md.Get("filter.go")
		if len(metadata) == 0 {
			return f.fail("Metadata should not be empty")
		}

		k, ok := metadata["foo"]
		if !ok {
			return f.fail("Metadata foo should be found")
		}

		if fmt.Sprint(k) != "bar" {
			return f.fail("Metadata foo has unexpected value %v", k)
		}
	}

	fs := f.callbacks.StreamInfo().FilterState()
	fs.SetString("go_state_test_key", "go_state_test_value", api.StateTypeReadOnly, api.LifeSpanRequest, api.SharedWithUpstreamConnection)

	val := fs.GetString("go_state_test_key")
	header.Add("go-state-test-header-key", val)

	if strings.Contains(f.localreplay, "decode-header") {
		return f.sendLocalReply("decode-header")
	}

	header.Range(func(key, value string) bool {
		if key == ":path" && value != f.path {
			f.fail("path not match in Range")
			return false
		}
		return true
	})

	header.RangeWithCopy(func(key, value string) bool {
		if key == ":path" && value != f.path {
			f.fail("path not match in RangeWithCopy")
			return false
		}
		return true
	})

	origin, found := header.Get("x-test-header-0")
	hdrs := header.Values("x-test-header-0")
	if found {
		if origin != hdrs[0] {
			return f.fail("Values return incorrect data %v", hdrs)
		}
	} else if hdrs != nil {
		return f.fail("Values return unexpected data %v", hdrs)
	}

	if found {
		upperCase, _ := header.Get("X-Test-Header-0")
		if upperCase != origin {
			return f.fail("Get should be case-insensitive")
		}
		upperCaseHdrs := header.Values("X-Test-Header-0")
		if hdrs[0] != upperCaseHdrs[0] {
			return f.fail("Values should be case-insensitive")
		}
	}

	header.Add("UpperCase", "header")
	if hdr, _ := header.Get("uppercase"); hdr != "header" {
		return f.fail("Add should be case-insensitive")
	}
	header.Set("UpperCase", "header")
	if hdr, _ := header.Get("uppercase"); hdr != "header" {
		return f.fail("Set should be case-insensitive")
	}
	header.Del("UpperCase")
	if hdr, _ := header.Get("uppercase"); hdr != "" {
		return f.fail("Del should be case-insensitive")
	}

	header.Add("existed-header", "bar")
	header.Add("newly-added-header", "foo")
	header.Add("newly-added-header", "bar")

	header.Set("test-x-set-header-0", origin)
	header.Del("x-test-header-1")
	header.Set("req-route-name", f.callbacks.StreamInfo().GetRouteName())
	header.Set("req-downstream-local-address", f.callbacks.StreamInfo().DownstreamLocalAddress())
	header.Set("req-downstream-remote-address", f.callbacks.StreamInfo().DownstreamRemoteAddress())
	if !endStream && strings.Contains(f.databuffer, "decode-header") {
		return api.StopAndBuffer
	}

	if f.panic == "decode-header" {
		badcode()
	}
	return api.Continue
}

// test: get, set, append, prepend
func (f *filter) decodeData(buffer api.BufferInstance, endStream bool) api.StatusType {
	if f.sleep || f.data_sleep {
		time.Sleep(time.Millisecond * 100) // sleep 100 ms
	}
	if strings.Contains(f.localreplay, "decode-data") {
		return f.sendLocalReply("decode-data")
	}
	f.req_body_length += uint64(buffer.Len())
	if buffer.Len() != 0 {
		data := buffer.String()
		if string(buffer.Bytes()) != data {
			return f.sendLocalReply(fmt.Sprintf("data in bytes: %s vs data in string: %s",
				string(buffer.Bytes()), data))
		}

		buffer.SetString(strings.ToUpper(data))
		buffer.AppendString("_append")
		buffer.PrependString("prepend_")
	}
	if !endStream && strings.Contains(f.databuffer, "decode-data") {
		return api.StopAndBuffer
	}

	if f.panic == "decode-data" {
		badcode()
	}
	if f.badapi {
		// set header after header continued will panic with the ErrInvalidPhase error message.
		f.header.Set("foo", "bar")
	}
	return api.Continue
}

func (f *filter) decodeTrailers(trailers api.RequestTrailerMap) api.StatusType {
	if f.sleep {
		time.Sleep(time.Millisecond * 100) // sleep 100 ms
	}
	if strings.Contains(f.localreplay, "decode-trailer") {
		return f.sendLocalReply("decode-trailer")
	}

	trailers.Add("existed-trailer", "bar")
	trailers.Set("x-test-trailer-0", "bar")
	trailers.Del("x-test-trailer-1")

	if trailers.GetRaw("existed-trailer") == "foo" {
		trailers.Add("x-test-trailer-2", "bar")
	}

	upperCase, _ := trailers.Get("X-Test-Trailer-0")
	if upperCase != "bar" {
		return f.fail("Get should be case-insensitive")
	}
	upperCaseHdrs := trailers.Values("X-Test-Trailer-0")
	if upperCaseHdrs[0] != "bar" {
		return f.fail("Values should be case-insensitive")
	}

	trailers.Add("UpperCase", "trailers")
	if hdr, _ := trailers.Get("uppercase"); hdr != "trailers" {
		return f.fail("Add should be case-insensitive")
	}
	trailers.Set("UpperCase", "trailers")
	if hdr, _ := trailers.Get("uppercase"); hdr != "trailers" {
		return f.fail("Set should be case-insensitive")
	}
	trailers.Del("UpperCase")
	if hdr, _ := trailers.Get("uppercase"); hdr != "" {
		return f.fail("Del should be case-insensitive")
	}

	if f.panic == "decode-trailer" {
		badcode()
	}
	return api.Continue
}

func (f *filter) encodeHeaders(header api.ResponseHeaderMap, endStream bool) api.StatusType {
	if f.sleep {
		time.Sleep(time.Millisecond * 100) // sleep 100 ms
	}
	if strings.Contains(f.localreplay, "encode-header") {
		return f.sendLocalReply("encode-header")
	}

	if protocol, ok := f.callbacks.StreamInfo().Protocol(); ok {
		header.Set("rsp-protocol", protocol)
	}
	if code, ok := f.callbacks.StreamInfo().ResponseCode(); ok {
		header.Set("rsp-response-code", strconv.Itoa(int(code)))
	}
	if details, ok := f.callbacks.StreamInfo().ResponseCodeDetails(); ok {
		header.Set("rsp-response-code-details", details)
	}
	if upstream_host_address, ok := f.callbacks.StreamInfo().UpstreamRemoteAddress(); ok {
		header.Set("rsp-upstream-host", upstream_host_address)
	}
	if upstream_cluster_name, ok := f.callbacks.StreamInfo().UpstreamClusterName(); ok {
		header.Set("rsp-upstream-cluster", upstream_cluster_name)
	}

	origin, found := header.Get("x-test-header-0")
	hdrs := header.Values("x-test-header-0")
	if found {
		if origin != hdrs[0] {
			return f.fail("Values return incorrect data %v", hdrs)
		}
	} else if hdrs != nil {
		return f.fail("Values return unexpected data %v", hdrs)
	}

	if status, ok := header.Status(); ok {
		header.Add("rsp-status", strconv.Itoa(status))
	}

	header.Add("existed-header", "bar")
	header.Add("newly-added-header", "foo")
	header.Add("newly-added-header", "bar")

	header.Set("test-x-set-header-0", origin)
	header.Del("x-test-header-1")
	header.Set("test-req-body-length", strconv.Itoa(int(f.req_body_length)))
	header.Set("test-query-param-foo", f.query_params.Get("foo"))
	header.Set("test-scheme", f.scheme)
	header.Set("test-method", f.method)
	header.Set("test-path", f.path)
	header.Set("test-host", f.host)
	header.Set("test-log-level", f.callbacks.LogLevel().String())
	header.Set("rsp-route-name", f.callbacks.StreamInfo().GetRouteName())
	header.Set("rsp-filter-chain-name", f.callbacks.StreamInfo().FilterChainName())
	header.Set("rsp-attempt-count", strconv.Itoa(int(f.callbacks.StreamInfo().AttemptCount())))
	if name, ok := f.callbacks.StreamInfo().VirtualClusterName(); ok {
		header.Set("rsp-virtual-cluster-name", name)
	} else {
		header.Set("rsp-virtual-cluster-name", "not found")
	}

	if f.panic == "encode-header" {
		badcode()
	}
	return api.Continue
}

func (f *filter) encodeData(buffer api.BufferInstance, endStream bool) api.StatusType {
	if f.sleep || f.data_sleep {
		time.Sleep(time.Millisecond * 100) // sleep 100 ms
	}
	if strings.Contains(f.localreplay, "encode-data") {
		return f.sendLocalReply("encode-data")
	}
	data := buffer.String()
	buffer.SetString(strings.ToUpper(data))

	if f.panic == "encode-data" {
		badcode()
	}
	return api.Continue
}

func (f *filter) encodeTrailers(trailers api.ResponseTrailerMap) api.StatusType {
	if f.sleep {
		time.Sleep(time.Millisecond * 100) // sleep 100 ms
	}
	if strings.Contains(f.localreplay, "encode-trailer") {
		return f.sendLocalReply("encode-trailer")
	}

	if f.panic == "encode-trailer" {
		badcode()
	}
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

func (f *filter) DecodeData(buffer api.BufferInstance, endStream bool) api.StatusType {
	if f.async {
		go func() {
			defer f.callbacks.RecoverPanic()

			status := f.decodeData(buffer, endStream)
			if status != api.LocalReply {
				f.callbacks.Continue(status)
			}
		}()
		return api.Running
	} else {
		status := f.decodeData(buffer, endStream)
		return status
	}
}

func (f *filter) DecodeTrailers(trailers api.RequestTrailerMap) api.StatusType {
	if f.async {
		go func() {
			defer f.callbacks.RecoverPanic()

			status := f.decodeTrailers(trailers)
			if status != api.LocalReply {
				f.callbacks.Continue(status)
			}
		}()
		return api.Running
	} else {
		status := f.decodeTrailers(trailers)
		return status
	}
}

func (f *filter) EncodeHeaders(header api.ResponseHeaderMap, endStream bool) api.StatusType {
	if f.async {
		go func() {
			defer f.callbacks.RecoverPanic()

			status := f.encodeHeaders(header, endStream)
			if status != api.LocalReply {
				f.callbacks.Continue(status)
			}
		}()
		return api.Running
	} else {
		status := f.encodeHeaders(header, endStream)
		return status
	}
}

func (f *filter) EncodeData(buffer api.BufferInstance, endStream bool) api.StatusType {
	if f.async {
		go func() {
			defer f.callbacks.RecoverPanic()

			status := f.encodeData(buffer, endStream)
			if status != api.LocalReply {
				f.callbacks.Continue(status)
			}
		}()
		return api.Running
	} else {
		status := f.encodeData(buffer, endStream)
		return status
	}
}

func (f *filter) EncodeTrailers(trailers api.ResponseTrailerMap) api.StatusType {
	if f.async {
		go func() {
			defer f.callbacks.RecoverPanic()

			status := f.encodeTrailers(trailers)
			if status != api.LocalReply {
				f.callbacks.Continue(status)
			}
		}()
		return api.Running
	} else {
		status := f.encodeTrailers(trailers)
		return status
	}
}

func (f *filter) OnLog() {
	api.LogError("call log in OnLog")
}

func (f *filter) OnDestroy(reason api.DestroyReason) {
}
