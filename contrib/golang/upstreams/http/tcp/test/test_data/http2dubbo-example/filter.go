package main

import (
	"encoding/binary"
	"encoding/json"
	"strings"

	"dubbo.apache.org/dubbo-go/v3/common/constant"
	"dubbo.apache.org/dubbo-go/v3/filter/generic/generalizer"
	"github.com/envoyproxy/envoy/contrib/golang/common/go/api"

	hessian "github.com/apache/dubbo-go-hessian2"
)

type tcpUpstreamFilter struct {
	api.EmptyTcpUpstreamFilter

	callbacks api.TcpUpstreamCallbackHandler
	config    *config
}

const (
	DEFAULT_METHOD = "method-default"
)

/*
	EncodeData Usage:
	1. construct body from http to body
	2. change dubbo method by envoy cluster_name or envoy router_name
	3. set remote half close for conn
	4. set envoy self not half close for conn
*/
func (f *tcpUpstreamFilter) EncodeData(buffer api.BufferInstance, endOfStream bool) bool {

    // =========== step 1: get dubbo args from http body =========== // 
	dubboArgs := make(map[string]interface{}, 0)
	_ = json.Unmarshal(buffer.Bytes(), dubboArgs)


    // =========== step 2: get dubbo req method by envoy cluster-name =========== // 
	dubboMethod := DEFAULT_METHOD
	clusterName, _ := f.callbacks.StreamInfo().VirtualClusterName()
	if clusterName == f.config.clusterNameForMock {
		dubboMethod = fmt.Sprintf("method-for-cluster-%s", clusterName)
	}


    // =========== step 3: construct dubbo body for upstream req =========== // 
	buf := httpBodyToDubbo(dubboMethod, dubboArgs)
	_ = buffer.Set(buf)

	
    // =========== step 4: set remote half close by envoy router name =========== // 
	if f.callbacks.StreamInfo().GetRouteName() == f.config.routerNameForHalfClose {
		f.callbacks.EnableHalfClose(true)
	}


    // =========== step 5: set envoy self not to half close conn =========== // 
	if !f.config.envoySelfEnableHalfClose {
		return false
	} 

	return true
	
}

const (
	DUBBO_MAGIC_SIZE    = 2
	DUBBO_LENGTH_OFFSET = 12
	DUBBO_HEADER_SIZE   = 16

	DUBBO_PROTOCOL_UPSTREAM_MAGIN_ERROR string = "protocol_magic_error"
	DUBBO_PROTOCOL_HEADER_LENGTH_ERROR string = "magin_error"
	DUBBO_PROTOCOL_TOO_MANY_DATA string = "too_many_data"
)

/*
	OnUpstreamData Usage:
	1. verify dubbo frame format
	2. aggregate multi dubbo frame when server has big response
	3. convert body from dubbo to http 
*/
func (f *tcpUpstreamFilter) OnUpstreamData(buffer api.BufferInstance, endOfStream bool) api.UpstreamDataStatus {

    // =========== step 1: verify dubbo frame format =========== // 
	if buffer.Len() < DUBBO_MAGIC_SIZE || binary.BigEndian.Uint16(buffer.Bytes()) != hessian.MAGIC {
		api.LogErrorf("[OnUpstreamData] Protocol Magic error")
		buffer.SetString(DUBBO_PROTOCOL_UPSTREAM_MAGIN_ERROR)
		return api.UpstreamDataFailure
	}
	if buffer.Len() < hessian.HEADER_LENGTH {
		api.LogErrorf("[OnUpstreamData] Protocol Header length error")
		buffer.SetString(DUBBO_PROTOCOL_HEADER_LENGTH_ERROR)
		return api.UpstreamDataFailure
	}
	bodyLength := binary.BigEndian.Uint32(buffer.Bytes()[DUBBO_LENGTH_OFFSET:])
	if buffer.Len() > (int(bodyLength) + hessian.HEADER_LENGTH) {
		api.LogInfof("[OnUpstreamData] TooManyData for Body")
		buffer.SetString(DUBBO_PROTOCOL_TOO_MANY_DATA)
		return api.UpstreamDataFailure
	}


    // =========== step 2: aggregate multi dubbo frame when server has big response =========== // 
	if buffer.Len() < (int(bodyLength) + hessian.HEADER_LENGTH) {
		api.LogInfof("[OnUpstreamData] NeedMoreData for Body")
		return api.UpstreamDataContinue
	}
	api.LogInfof("[OnUpstreamData] finish Aggregation for Body")


    // =========== step 3: convert body from dubbo to http =========== // 
	b := buffer.Bytes()[DUBBO_HEADER_SIZE:]
	decoder := hessian.NewDecoder(b)
	rsp, _ := decoder.Decode()
	_ = buffer.Set(rsp)

	return api.UpstreamDataFinish
}

func (*tcpUpstreamFilter) OnDestroy(reason api.DestroyReason) {
	api.LogInfof("golang-test [http2rpc][OnDestroy] , reason: %+v", reason)
}

func httpBodyToDubbo(methodName string, dubboArgs map[string]interface{}) []byte {
	types := make([]string, 0, len(dubboArgs))
	args := make([]hessian.Object, 0, len(dubboArgs))
	attchments := map[string]interface{}{
		constant.GenericKey:   constant.GenericSerializationDefault,
		constant.InterfaceKey: "com.alibaba.nacos.example.dubbo.service.DemoService",
		constant.MethodKey:    methodName,
	}

	g := getGeneralizer(constant.GenericSerializationDefault)
	for _, arg := range dubboArgs {
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
		methodName,
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

	rsp := remoting.NewPendingResponse(req.ID)
	rsp.Reply = newIvc.Reply()
	remoting.AddPendingResponse(rsp)
	req.Data = newIvc
	req.Event = false
	req.TwoWay = true
	buf, _ := codec.EncodeRequest(req)

	return buf.Bytes()
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
