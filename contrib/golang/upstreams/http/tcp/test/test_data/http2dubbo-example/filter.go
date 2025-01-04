package main

import (
	"bytes"
	"encoding/binary"
	"encoding/json"
	"fmt"
	"math/rand"
	"strconv"
	"strings"

	"dubbo.apache.org/dubbo-go/v3/common/constant"
	"dubbo.apache.org/dubbo-go/v3/filter/generic/generalizer"
	"dubbo.apache.org/dubbo-go/v3/protocol"
	"dubbo.apache.org/dubbo-go/v3/remoting"
	"github.com/envoyproxy/envoy/contrib/golang/common/go/api"

	dubbo2 "dubbo.apache.org/dubbo-go/v3/protocol/dubbo"
	invocation2 "dubbo.apache.org/dubbo-go/v3/protocol/invocation"
	hessian "github.com/apache/dubbo-go-hessian2"
)

const (
	DefaultMethodName    = "sayName"
	DefaultInterfaceName = "com.alibaba.nacos.example.dubbo.service.DemoService"
	GrayInterfaceName    = "com.alibaba.nacos.example.dubbo.service.DemoService"
)

type httpTcpBridge struct {
	api.PassThroughHttpTcpBridge

	callbacks api.HttpTcpBridgeCallbackHandler
	config    *config

	dubboMethod    string
	dubboInterface string

	alreadySendDataInEncodeHeaders bool
}

/*
*
  - EncodeHeaders Usages:
  - 1. get dubboMethod, dubboInterface from headers
  - 2. construct & set data for sending to upstream
    *
  - @param headerMap supplies req header for read only.
  - @param bufferForUpstreamData supplies data to be set for sending to upstream.
  - @param endOfStream if end of stream.
  - @return api.SendDataStatus tell c++ side next action:
    1.HttpTcpBridgeContinue: will go to encodeData, encodeData will streaming get each_data_piece.
    2.HttpTcpBridgeStopAndBuffer: will go to encodeData, encodeData will buffer whole data, go side in encodeData get whole data one-off.
    3.HttpTcpBridgeSendData: directly send data to upstream, and encodeData will not be called even when downstream_req has body.

*
*/
func (f *httpTcpBridge) EncodeHeaders(headerMap api.RequestHeaderMap, dataToUpstream api.BufferInstance, endOfStream bool) api.HttpTcpBridgeStatus {
	api.LogInfof("[EncodeHeaders] come, endStream: %v", endOfStream)
	// =========== step 1: get dubbo method and interface from http header =========== //
	dubboMethod, _ := headerMap.Get("dubbo_method")
	dubboInterface, _ := headerMap.Get("dubbo_interface")
	// panic("qqq")

	// go func() {
	// 	_, _ = headerMap.Get("dubbo_interface")
	// }()
	// go func() {
	// 	_, _ = headerMap.Get("dubbo_interface")
	// }()
	// go func() {
	// 	_, _ = headerMap.Get("dubbo_interface")
	// }()
	// time.Sleep(100 * time.Millisecond)

	// =========== step 2: if body is empty, or get unexpected header, directly send data to upstream =========== //
	if endOfStream || (dubboMethod == "" || dubboInterface == "") {
		// get mock dubbo frame
		buf := transformToDubboFrame("", "", map[string]string{"name": "mock"})
		// for test buffer API
		dataToUpstream.String()
		dataToUpstream.Reset()
		// directly send data to upstream without EncodeData
		dataToUpstream.Set(buf.Bytes())
		headerMap.Range(func(key, value string) bool {
			api.LogInfof("EncodeHeaders key: %s, value: %s", key, value)
			return true
		})

		// panic("hhh")

		f.alreadySendDataInEncodeHeaders = true
		return api.HttpTcpBridgeContinue
	}

	f.dubboMethod = dubboMethod
	f.dubboInterface = dubboInterface

	// panic("ggg")

	// for full-buffer
	return api.HttpTcpBridgeStopAndBuffer

	// for streaming
	// return api.HttpTcpBridgeContinue
}

/*
*
  - EncodeData Usages:
  - 1. get dubboArgs from http body
  - 2. change dubboInterface for gray_traffic by router_name
  - 3. construct & set data for sending to upstream
  - 4. set envoy-self half close for conn
    *
  - @param bufferForUpstreamData supplies data to be set for sending to upstream.
  - @param endOfStream if end of stream.
  - @return api.SendDataStatus tell c++ side next action:
    1.HttpTcpBridgeContinue: streaming send data to upstream, go side get each_data_piece, may be called multipled times.
    2.HttpTcpBridgeStopAndBuffer: buffer further whole data, go side in encodeData get whole data one-off. (Be careful: cannot be used when end_stream=true).

*
*/
func (f *httpTcpBridge) EncodeData(buffer api.BufferInstance, endOfStream bool) api.HttpTcpBridgeStatus {
	if f.alreadySendDataInEncodeHeaders {
		return api.HttpTcpBridgeContinue
	}
	// panic("www")
	api.LogInfof("[EncodeData] come, buf: %s, len: %d, endStream: %v", buffer, buffer.Len(), endOfStream)
	// =========== step 1: get dubboArgs from http body =========== //
	dubboArgs := make(map[string]string, 0)
	err := json.Unmarshal(buffer.Bytes(), &dubboArgs)
	if err != nil {
		api.LogInfof("[EncodeData] json Unmarshal err: %s", err)
	}

	// =========== step 2: assign dubboInterface for gray traffic by router =========== //
	if f.callbacks.GetRouteName() == f.config.routerNameForGrayTraffic {
		f.dubboInterface = GrayInterfaceName
	}

	// go func() {
	// 	_ = f.callbacks.GetRouteName()
	// }()
	// go func() {
	// 	_ = f.callbacks.GetRouteName()
	// }()
	// go func() {
	// 	_ = f.callbacks.GetVirtualClusterName()
	// }()
	// go func() {
	// 	_ = f.callbacks.GetVirtualClusterName()
	// }()
	// go func() {
	// 	f.callbacks.SetSelfHalfCloseForUpstreamConn(false)
	// }()
	// go func() {
	// 	f.callbacks.SetSelfHalfCloseForUpstreamConn(false)
	// }()

	// =========== step 3: construct dubbo frame with dubboMethod, dubboInterface, dubboArgs for upstream req =========== //
	buf := transformToDubboFrame(f.dubboMethod, f.dubboInterface, dubboArgs)
	_ = buffer.Set(buf.Bytes())

	// go func() {
	// 	_ = buffer.Set(buf.Bytes())
	// }()
	// go func() {
	// 	_ = buffer.Set(buf.Bytes())
	// }()
	// go func() {
	// 	_ = buffer.Set(buf.Bytes())
	// }()
	// time.Sleep(100 * time.Millisecond)

	// =========== step 4: set self half close for upstream conn =========== //
	if !f.config.enableTunneling {
		f.callbacks.SetSelfHalfCloseForUpstreamConn(true)
	}

	// panic("eee")

	// for full-buffer from encodeHeaders HttpTcpBridgeStopAndBuffer
	return api.HttpTcpBridgeContinue

	// for streaming from encodeHeaders HttpTcpBridgeContinue
	// if endOfStream {
	// 	return api.HttpTcpBridgeContinue
	// } else {
	// 	return api.HttpTcpBridgeStopAndBuffer
	// }
}

const (
	DUBBO_LENGTH_OFFSET = 12
	DUBBO_MAGIC_SIZE    = 2
	DUBBO_HEADER_SIZE   = 16

	DUBBO_PROTOCOL_UPSTREAM_MAGIN_ERROR string = "protocol_magic_error"
	DUBBO_PROTOCOL_HEADER_LENGTH_ERROR  string = "header_length_error"
)

/*
*
  - OnUpstreamData Usages:
  - 1. verify dubbo frame format
  - 2. aggregate multi dubbo frame when server has big response
  - 3. convert body from dubbo to http
  - 4. construct http response header
  - 5. set label for specify-cluster
    *
  - @param responseHeaderForSet to construct & set http response header.
  - @param buffer supplies data to be set for sending to downstream.
  - @param endOfStream if end of stream.
  - @return api.ReceiveDataStatus tell c++ side next action:
    1.HttpTcpBridgeContinue: go side in onUpstreamData will get each_data_piece, pass data and headers to downstream streaming.
    2.HttpTcpBridgeStopAndBuffer: every data trigger will call go side, and go side get buffer data from start.
    3.HttpTcpBridgeSendData: send data and headers to downstream which means the whole resp to http is finished.

*
*/
func (f *httpTcpBridge) OnUpstreamData(responseHeaderForSet api.ResponseHeaderMap, buffer api.BufferInstance, endOfStream bool) api.HttpTcpBridgeStatus {
	api.LogInfof("[OnUpstreamData] receive body, len: %d", buffer.Len())

	// panic("rrr")

	responseHeaderForSet.Set("a", "1")
	responseHeaderForSet.Set("b", "1")
	responseHeaderForSet.Set(strconv.Itoa(rand.Intn(100)), "a")
	responseHeaderForSet.Range(func(key, value string) bool {
		api.LogInfof("[OnUpstreamData] key: %s,  value : %s", key, value)
		return true
	})

	// =========== step 1: verify dubbo frame format =========== //
	if buffer.Len() < DUBBO_MAGIC_SIZE || binary.BigEndian.Uint16(buffer.Bytes()) != hessian.MAGIC {
		api.LogErrorf("[OnUpstreamData] Protocol Magic error, %s", buffer.Bytes())
		responseHeaderForSet.Set(":status", "500")
		buffer.SetString(DUBBO_PROTOCOL_UPSTREAM_MAGIN_ERROR)
		return api.HttpTcpBridgeEndStream
	}
	if buffer.Len() < hessian.HEADER_LENGTH {
		api.LogErrorf("[OnUpstreamData] Protocol Header length error")
		responseHeaderForSet.Set(":status", "500")
		buffer.SetString(DUBBO_PROTOCOL_HEADER_LENGTH_ERROR)
		return api.HttpTcpBridgeEndStream
	}
	bodyLength := binary.BigEndian.Uint32(buffer.Bytes()[DUBBO_LENGTH_OFFSET:])

	// =========== step 2: aggregate multi dubbo frame when server has big response =========== //
	if buffer.Len() < (int(bodyLength) + hessian.HEADER_LENGTH) {
		api.LogInfof("[OnUpstreamData] NeedMoreData for Body")
		if endOfStream {
			api.LogErrorf("[OnUpstreamData] upstream bad endOfStream")
			buffer.SetString("bad endOfStream")
			return api.HttpTcpBridgeEndStream
		}
		return api.HttpTcpBridgeStopAndBuffer
	}
	api.LogInfof("[OnUpstreamData] finish Aggregation for Body")

	// =========== step 3: construct http response body =========== //
	b := buffer.Bytes()[DUBBO_HEADER_SIZE:]
	decoder := hessian.NewDecoder(b)
	decoder.Decode()
	rsp, _ := decoder.Decode()
	bodyBytes := []byte(fmt.Sprintf("%s", rsp))
	_ = buffer.Set(bodyBytes)

	// go func() {
	// 	_ = buffer.Set(bodyBytes)
	// }()
	// go func() {
	// 	_ = buffer.Set(bodyBytes)
	// }()
	// go func() {
	// 	_ = buffer.Set(bodyBytes)
	// }()
	// time.Sleep(100 * time.Millisecond)

	// =========== step 4: construct http response header =========== //
	responseHeaderForSet.Set(":status", "200")
	responseHeaderForSet.Set("content-type", "application/json; charset=utf-8")
	responseHeaderForSet.Set("extension", "golang-tcp-upstream")

	// =========== step 5: set label for specify-cluster =========== //
	if f.callbacks.GetVirtualClusterName() == f.config.clusterNameForSpecialLabel {
		responseHeaderForSet.Set("lable-for-special-cluster", f.config.clusterNameForSpecialLabel)
	}
	responseHeaderForSet.Del("a")

	// go func() {
	// 	responseHeaderForSet.Set("c", "1")
	// }()
	// go func() {
	// 	responseHeaderForSet.Set("c", "2")
	// }()
	// go func() {
	// 	responseHeaderForSet.Set("c", "3")
	// }()
	// time.Sleep(100 * time.Millisecond)

	// panic("fff")

	return api.HttpTcpBridgeEndStream
}

/*
*
  - OnDestroy Usages:
  - 1. do something when destroy request
    *

*
*/
func (*httpTcpBridge) OnDestroy() {
	// panic("iii")
	api.LogInfof("[OnDestroy] , httpTcpBridge destroy")
}

func transformToDubboFrame(methodName, interfaceName string, dubboArgsFromHttp map[string]string) *bytes.Buffer {
	if methodName == "" {
		methodName = DefaultMethodName
	}
	if interfaceName == "" {
		interfaceName = DefaultInterfaceName
	}
	if len(dubboArgsFromHttp) == 0 {
		dubboArgsFromHttp = map[string]string{
			"name": "default-name",
		}
	}

	types := make([]string, 0, len(dubboArgsFromHttp))
	args := make([]hessian.Object, 0, len(dubboArgsFromHttp))
	attchments := map[string]interface{}{
		constant.GenericKey:   constant.GenericSerializationDefault,
		constant.InterfaceKey: interfaceName,
		constant.MethodKey:    methodName,
	}

	g := getGeneralizer(constant.GenericSerializationDefault)
	for _, arg := range dubboArgsFromHttp {
		typ, _ := g.GetType(arg)
		obj, _ := g.Generalize(arg)
		types = append(types, typ)
		args = append(args, obj)
	}

	// construct a new invocation for generic call
	newArgs := []interface{}{
		methodName,
		types,
		args,
	}
	var newIvc protocol.Invocation = invocation2.NewRPCInvocation(constant.Generic, newArgs, attchments)
	newIvc.SetAttachment(constant.PathKey, interfaceName)
	newIvc.SetAttachment(constant.InterfaceKey, interfaceName)
	newIvc.SetAttachment(constant.VersionKey, "1.0.0")

	req := remoting.NewRequest("2.0.2")
	req.Data = &newIvc
	req.Event = false
	req.TwoWay = true

	codec := &dubbo2.DubboCodec{}
	buf, _ := codec.EncodeRequest(req)

	return buf
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
