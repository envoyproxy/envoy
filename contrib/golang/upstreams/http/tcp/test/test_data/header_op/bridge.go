package header_op

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
		if authority, ok := headerMap.Get(":authority"); ok {
			dataForSet.SetString(fmt.Sprintf("%s-http-to-tcp-encode_headers-%s", Name, authority))
		}
	}
	return api.HttpTcpBridgeContinue
}

func (f *httpTcpBridge) EncodeData(buffer api.BufferInstance, endOfStream bool) api.HttpTcpBridgeStatus {

	return api.HttpTcpBridgeContinue
}

func (f *httpTcpBridge) OnUpstreamData(responseHeaderForSet api.ResponseHeaderMap, buffer api.BufferInstance, endOfStream bool) api.HttpTcpBridgeStatus {

	buffer.SetString(fmt.Sprintf("%s-tcp-to-http:%s", Name, buffer.String()))

	responseHeaderForSet.Set(":status", "202")

	return api.HttpTcpBridgeEndStream
}

func (*httpTcpBridge) OnDestroy() {
	api.LogInfof("[OnDestroy] , httpTcpBridge destroy")
}
