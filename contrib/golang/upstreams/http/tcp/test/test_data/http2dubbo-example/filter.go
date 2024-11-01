package main

import (
	"bytes"
	"encoding/binary"
	"encoding/json"
	"fmt"
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

type tcpUpstreamFilter struct {
	api.EmptyTcpUpstreamFilter

	callbacks       api.TcpUpstreamCallbackHandler
	config          *config
	alreadySendData bool
	httpReqBodyBuf  *bytes.Buffer

	dubboMethod    string
	dubboInterface string
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
  - @return EndStream tell c++ side whether to end the stream for half closing the conn.

*
*/
func (f *tcpUpstreamFilter) EncodeHeaders(headerMap api.RequestHeaderMap, bufferForUpstreamData api.BufferInstance, endOfStream bool) api.SendDataStatus {

	// =========== step 1: get dubbo method and interface from http header =========== //
	dubboMethod, _ := headerMap.Get("dubbo_method")
	dubboInterface, _ := headerMap.Get("dubbo_interface")

	// =========== step 2: if body is empty, or get unexpected header, directly send data to upstream =========== //
	contentLength, _ := headerMap.Get("content-length")
	if contentLength == "" || (dubboMethod == "" || dubboInterface == "") {
		// get mock dubbo frame
		buf := transformToDubboFrame("", "", map[string]string{"name": "mock"})
		// directly send data to upstream without EncodeData
		bufferForUpstreamData.Set(buf.Bytes())
		headerMap.Range(func(key, value string) bool {
			api.LogInfof("EncodeHeaders key: %s, value: %s", key, value)
			return true
		})
		f.alreadySendData = true
		return api.SendDataWithTunneling
	}

	f.dubboMethod = dubboMethod
	f.dubboInterface = dubboInterface

	return api.NotSendData
}

/*
*
  - EncodeData Usages:
  - 1. append data when streaming get data from downstream
  - 2. get dubboArgs from http body
  - 3. change dubboInterface for gray_traffic by router_name
  - 4. construct & set data for sending to upstream
  - 5. set remote half close for conn by cluster_name
  - 6. set envoy-self half close for conn
    *
  - @param bufferForUpstreamData supplies data to be set for sending to upstream.
  - @param endOfStream if end of stream.
  - @return EndStream tell c++ side whether to end the stream for half closing the conn.

*
*/
func (f *tcpUpstreamFilter) EncodeData(buffer api.BufferInstance, endOfStream bool) api.SendDataStatus {
	if f.alreadySendData { // if already send data to upstream, we will not send here
		return api.NotSendData
	}

	// =========== step 1: append data when streaming get data from downstream =========== //
	f.httpReqBodyBuf.Write(buffer.Bytes())
	if !endOfStream {
		return api.NotSendData
	}

	// =========== step 2: get dubboArgs from http body =========== //
	dubboArgs := make(map[string]string, 0)
	_ = json.Unmarshal(f.httpReqBodyBuf.Bytes(), &dubboArgs)

	// =========== step 3: assign dubboInterface for gray traffic by router =========== //
	if f.callbacks.StreamInfo().GetRouteName() == f.config.routerNameForGrayTraffic {
		f.dubboInterface = GrayInterfaceName
	}

	// =========== step 4: construct dubbo frame with dubboMethod, dubboInterface, dubboArgs for upstream req =========== //
	buf := transformToDubboFrame(f.dubboMethod, f.dubboInterface, dubboArgs)
	_ = buffer.Set(buf.Bytes())

	// =========== step 5: enable remote half close conn by cluster =========== //
	clusterName, _ := f.callbacks.StreamInfo().VirtualClusterName()
	if clusterName == f.config.clusterNameForEnableRemoteHalfClose {
		f.callbacks.EnableHalfClose(true)
	}

	// =========== step 6: set upstream conn tunneling =========== //
	if !f.config.enableTunneling {
		return api.SendDataWithNotTunneling
	}

	return api.SendDataWithTunneling
}

const (
	DUBBO_LENGTH_OFFSET = 12
	DUBBO_MAGIC_SIZE    = 2
	DUBBO_HEADER_SIZE   = 16

	DUBBO_PROTOCOL_UPSTREAM_MAGIN_ERROR string = "protocol_magic_error"
	DUBBO_PROTOCOL_HEADER_LENGTH_ERROR  string = "magin_error"
	DUBBO_PROTOCOL_TOO_MANY_DATA        string = "too_many_data"
)

/*
*
  - OnUpstreamData Usages:
  - 1. verify dubbo frame format
  - 2. aggregate multi dubbo frame when server has big response
  - 3. convert body from dubbo to http
  - 4. construct http response header
    *
  - @param responseHeaderForSet to construct & set http response header.
  - @param buffer supplies data to be set for sending to downstream.
  - @param endOfStream if end of stream.
  - @return status tell c++ side next action:
    1.UpstreamDataContinue: need more data from upstream;
    2.UpstreamDataFinish: aggregate data success, return to downstream;
    3.UpstreamDataFailure: protocol error, directly return to downstream.

*
*/
func (f *tcpUpstreamFilter) OnUpstreamData(responseHeaderForSet api.ResponseHeaderMap, buffer api.BufferInstance, endOfStream bool) (status api.ReceiveDataStatus) {

	// =========== step 1: verify dubbo frame format =========== //
	if buffer.Len() < DUBBO_MAGIC_SIZE || binary.BigEndian.Uint16(buffer.Bytes()) != hessian.MAGIC {
		api.LogErrorf("[OnUpstreamData] Protocol Magic error")
		buffer.SetString(DUBBO_PROTOCOL_UPSTREAM_MAGIN_ERROR)
		return api.ReceiveDataFailure
	}
	if buffer.Len() < hessian.HEADER_LENGTH {
		api.LogErrorf("[OnUpstreamData] Protocol Header length error")
		buffer.SetString(DUBBO_PROTOCOL_HEADER_LENGTH_ERROR)
		return api.ReceiveDataFailure
	}
	bodyLength := binary.BigEndian.Uint32(buffer.Bytes()[DUBBO_LENGTH_OFFSET:])

	// =========== step 2: aggregate multi dubbo frame when server has big response =========== //
	if buffer.Len() < (int(bodyLength) + hessian.HEADER_LENGTH) {
		api.LogInfof("[OnUpstreamData] NeedMoreData for Body")
		return api.ReceiveDataContinue
	}
	api.LogInfof("[OnUpstreamData] finish Aggregation for Body")

	// =========== step 3: construct http response body =========== //
	b := buffer.Bytes()[DUBBO_HEADER_SIZE:]
	decoder := hessian.NewDecoder(b)
	decoder.Decode()
	rsp, _ := decoder.Decode()
	bodyBytes := []byte(fmt.Sprintf("%s", rsp))
	_ = buffer.Set(bodyBytes)

	// =========== step 4: construct http response header =========== //
	responseHeaderForSet.Set(":status", "200")
	responseHeaderForSet.Set("content-type", "application/json; charset=utf-8")
	responseHeaderForSet.Set("extension", "golang-tcp-upstream")

	return api.ReceiveDataFinish
}

/*
*
  - OnDestroy Usages:
  - 1. do something when destroy request
    *
  - @param reason tells destryp reason.

*
*/
func (*tcpUpstreamFilter) OnDestroy(reason api.DestroyReason) {
	if reason != api.Normal {
		api.LogInfof("[OnDestroy] , unexpected destroy, reason: %+v", reason)
	}
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
