package main

import (
	"encoding/binary"
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
	Dubbo_MOCK_BODY = "mock_body"
)

func (f *tcpUpstreamFilter) EncodeData(buffer api.BufferInstance, endOfStream bool) bool {
	clusterName, _ := f.callbacks.StreamInfo().VirtualClusterName()
	if clusterName == f.config.clusterNameForMock {
		buffer.SetString(Dubbo_MOCK_BODY)
	}

	if f.callbacks.StreamInfo().GetRouteName() == f.config.routerNameForHalfClose {
		f.callbacks.EnableHalfClose(true)
	}

	if f.config.envoySelfEnableHalfClose {
		return true
	} 
	return false
	
}

const (
	DUBBO_MAGIC_SIZE    = 2
	DUBBO_LENGTH_OFFSET = 12
	DUBBO_HEADER_SIZE   = 16

	DUBBO_PROTOCOL_UPSTREAM_MAGIN_ERROR string = "protocol_magic_error"
	DUBBO_PROTOCOL_HEADER_LENGTH_ERROR string = "magin_error"
	DUBBO_PROTOCOL_TOO_MANY_DATA string = "too_many_data"
)

func (f *tcpUpstreamFilter) OnUpstreamData(buffer api.BufferInstance, endOfStream bool) api.UpstreamDataStatus {
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

	if buffer.Len() < (int(bodyLength) + hessian.HEADER_LENGTH) {
		api.LogInfof("[OnUpstreamData] NeedMoreData for Body")
		return api.UpstreamDataContinue
	}

	api.LogInfof("[OnUpstreamData] finish Aggregation for Body")
	return api.UpstreamDataFinish
}

func (*tcpUpstreamFilter) OnDestroy(reason api.DestroyReason) {
	api.LogInfof("golang-test [http2rpc][OnDestroy] , reason: %+v", reason)
}
