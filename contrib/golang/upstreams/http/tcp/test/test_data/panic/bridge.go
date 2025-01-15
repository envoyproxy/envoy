package panic

import (
	"fmt"

	"github.com/envoyproxy/envoy/contrib/golang/common/go/api"
)

type httpTcpBridge struct {
	api.PassThroughHttpTcpBridge

	callbacks api.HttpTcpBridgeCallbackHandler
	config    *config
}

func (f *httpTcpBridge) EncodeHeaders(headerMap api.RequestHeaderMap, dataForSet api.BufferInstance, endOfStream bool) api.HttpTcpBridgeStatus {
	if endOfStream {
		panic(fmt.Errorf("panic in EncodeHeaders"))
	}

	return api.HttpTcpBridgeContinue
}

func (f *httpTcpBridge) EncodeData(buffer api.BufferInstance, endOfStream bool) api.HttpTcpBridgeStatus {
	if endOfStream {
		panic(fmt.Errorf("panic in EncodeData"))
	}

	return api.HttpTcpBridgeContinue
}

func (f *httpTcpBridge) OnUpstreamData(responseHeaderForSet api.ResponseHeaderMap, buffer api.BufferInstance, endOfStream bool) api.HttpTcpBridgeStatus {

	panic(fmt.Errorf("panic in EncodeData"))

}

func (*httpTcpBridge) OnDestroy() {
	api.LogInfof("[OnDestroy] , httpTcpBridge destroy")
}
