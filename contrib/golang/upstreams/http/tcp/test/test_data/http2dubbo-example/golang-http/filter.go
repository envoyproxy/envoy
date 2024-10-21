package main

import (
	"fmt"
	"strconv"

	"github.com/envoyproxy/envoy/contrib/golang/common/go/api"
	"dubbo.apache.org/dubbo-go/v3/common/constant"
	"dubbo.apache.org/dubbo-go/v3/filter/generic/generalizer"

	"encoding/binary"

	dubbo2 "dubbo.apache.org/dubbo-go/v3/protocol/dubbo"
	invocation2 "dubbo.apache.org/dubbo-go/v3/protocol/invocation"
	"dubbo.apache.org/dubbo-go/v3/remoting"

	hessian "github.com/apache/dubbo-go-hessian2"
	"strings"
)

var UpdateUpstreamBody = "upstream response body updated by the simple plugin"

// The callbacks in the filter, like `DecodeHeaders`, can be implemented on demand.
// Because api.PassThroughStreamFilter provides a default implementation.
type filter struct {
	api.PassThroughStreamFilter

	Header     api.RequestHeaderMap
	RespHeader api.ResponseHeaderMap

	callbacks api.FilterCallbackHandler
	path      string
	config    *config
}


// Callbacks which are called in request path
// The endStream is true if the request doesn't have body
func (f *filter) DecodeHeaders(header api.RequestHeaderMap, endStream bool) api.StatusType {
	api.LogInfo("[http2rpc][DecodeHeaders] start")
	api.LogInfo(fmt.Sprintf("[http2rpc][DecodeHeaders] route: %s", f.callbacks.StreamInfo().GetRouteName()))

	f.Header = header
	header.Set(":status", "200")

	api.LogInfo("[http2rpc][DecodeHeaders] end")
	return api.StopAndBuffer
}

// DecodeData might be called multiple times during handling the request body.
// The endStream is true when handling the last piece of the body.
func (f *filter) DecodeData(buffer api.BufferInstance, endStream bool) api.StatusType {
	api.LogInfo("[http2rpc][DecodeData] start")

	mtdname := "sayName"
	oldargs := map[string]interface{}{
		"name": "jackduxinxxx",
	}

	types := make([]string, 0, len(oldargs))
	args := make([]hessian.Object, 0, len(oldargs))
	attchments := map[string]interface{}{
		constant.GenericKey:   constant.GenericSerializationDefault,
		constant.InterfaceKey: "com.alibaba.nacos.example.dubbo.service.DemoService",
		constant.MethodKey:    mtdname,
	}

	g := getGeneralizer(constant.GenericSerializationDefault)

	for _, arg := range oldargs {
		// use the default generalizer(MapGeneralizer)
		typ, err := g.GetType(arg)
		if err != nil {
			api.LogErrorf("failed to get type, %v", err)
		}
		obj, err := g.Generalize(arg)
		if err != nil {
			api.LogErrorf("generalization failed, %v", err)
			return api.Continue
		}
		types = append(types, typ)
		args = append(args, obj)
	}

	// construct a new invocation for generic call
	newArgs := []interface{}{
		mtdname,
		types,
		args,
	}
	newIvc := invocation2.NewRPCInvocation(constant.Generic, newArgs, attchments)
	newIvc.SetAttachment(constant.PathKey, "com.alibaba.nacos.example.dubbo.service.DemoService")
	newIvc.SetAttachment(constant.InterfaceKey, "com.alibaba.nacos.example.dubbo.service.DemoService")
	newIvc.SetAttachment(constant.VersionKey, "1.0.0")
	api.LogInfo(fmt.Sprintf("newIvc: %+v", newIvc))

	codec := &dubbo2.DubboCodec{}
	req := remoting.NewRequest("2.0.2")

	// req.ID = 1
	rsp := remoting.NewPendingResponse(req.ID)
	rsp.Reply = newIvc.Reply()
	remoting.AddPendingResponse(rsp)

	req.Data = newIvc
	req.Event = false
	req.TwoWay = true
	buf, err := codec.EncodeRequest(req)
	if err != nil {
		api.LogErrorf("failed to encode request, req: %+v, buf: %+v, err: %+v", req.Data, buf, err)
		return api.LocalReply
	}

	_ = buffer.Set(buf.Bytes())

	api.LogInfo("[http2rpc][DecodeData] end")
	return api.Continue
}

func (f *filter) DecodeTrailers(trailers api.RequestTrailerMap) api.StatusType {
	// support suspending & resuming the filter in a background goroutine
	return api.Continue
}

// Callbacks which are called in response path
// The endStream is true if the response doesn't have body
func (f *filter) EncodeHeaders(header api.ResponseHeaderMap, endStream bool) api.StatusType {
	api.LogInfo("[http2rpc][EncodeHeaders] start")
	header.Set("Content-Type", "application/json; charset=utf-8")
	header.Set(":status", "300")
	// header.Set("Content-Length", "87")
	f.RespHeader = header
	if endStream {
		return api.Continue
	}
	api.LogInfo("[http2rpc][EncodeHeaders] end")
	return api.StopAndBuffer
}

const (
	DUBBO_MAGIC_SIZE        = 2
	DUBBO_REQUEST_ID_OFFSET = 4
	DUBBO_LENGTH_OFFSET     = 12
	DUBBO_HEADER_SIZE       = 16
)

// EncodeData might be called multiple times during handling the response body.
// The endStream is true when handling the last piece of the body.
func (f *filter) EncodeData(buffer api.BufferInstance, endStream bool) api.StatusType {
	api.LogInfo("[http2rpc][EncodeData] start")
	api.LogInfof("[http2rpc][EncodeData] data: %+v", buffer)
	api.LogInfof("[http2rpc][EncodeData] data: %+v", string(buffer.Bytes()[DUBBO_HEADER_SIZE:]))

	requestId := binary.BigEndian.Uint64(buffer.Bytes()[DUBBO_REQUEST_ID_OFFSET:])
	api.LogInfof("[http2rpc][EncodeData] requestID: %+v", requestId)

	b := buffer.Bytes()[DUBBO_HEADER_SIZE:]
	decoder := hessian.NewDecoder(b)
	_, err := decoder.Decode()
	if err != nil {
		panic(fmt.Sprintf("[http2rpc][EncodeResponse] Decode, err: %+v", err))
	}
	rsp, err := decoder.Decode()
	if err != nil {
		panic(fmt.Sprintf("[http2rpc][EncodeResponse] Decode-2, err: %+v", err))
	}
	api.LogInfof("[http2rpc][EncodeResponse] Decode, val: %+v", rsp)
	bodyBytes := []byte(fmt.Sprintf("%s", rsp))
	_ = buffer.Set(bodyBytes)
	// f.RespHeader.Set("Content-Length", buffer.Len())

	api.LogInfof("[http2rpc][EncodeData] end, length: %+v", buffer.Len())
	return api.Continue
}

func (f *filter) EncodeTrailers(trailers api.ResponseTrailerMap) api.StatusType {
	return api.Continue
}

// OnLog is called when the HTTP stream is ended on HTTP Connection Manager filter.

func (f *filter) OnLog(api.RequestHeaderMap, api.RequestTrailerMap, api.ResponseHeaderMap, api.ResponseTrailerMap) {
	code, _ := f.callbacks.StreamInfo().ResponseCode()
	respCode := strconv.Itoa(int(code))
	api.LogDebug(respCode)
}

// OnLogDownstreamStart is called when HTTP Connection Manager filter receives a new HTTP request
// (required the corresponding access log type is enabled)
func (f *filter) OnLogDownstreamStart(api.RequestHeaderMap) {
	// also support kicking off a goroutine here, like OnLog.
}

// OnLogDownstreamPeriodic is called on any HTTP Connection Manager periodic log record
// (required the corresponding access log type is enabled)
func (f *filter) OnLogDownstreamPeriodic(api.RequestHeaderMap, api.RequestTrailerMap, api.ResponseHeaderMap, api.ResponseTrailerMap) {
	// also support kicking off a goroutine here, like OnLog.
}

func (f *filter) OnDestroy(reason api.DestroyReason) {
	// One should not access f.callbacks here because the FilterCallbackHandler
	// is released. But we can still access other Go fields in the filter f.

	// goroutine can be used everywhere.
}

func (f *filter) OnStreamComplete() {

}

func getGeneralizer(generic string) (g generalizer.Generalizer) {
	switch strings.ToLower(generic) {
	case constant.GenericSerializationDefault:
		g = generalizer.GetMapGeneralizer()
	case constant.GenericSerializationGson:
		g = generalizer.GetGsonGeneralizer()

	default:
		api.LogInfof("\"%s\" is not supported, use the default generalizer(MapGeneralizer)", generic)
		g = generalizer.GetMapGeneralizer()
	}
	return
}
