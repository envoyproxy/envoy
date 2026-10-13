package local_reply

import (
	"fmt"
	"strings"

	"github.com/envoyproxy/envoy/contrib/golang/common/go/api"
)

type httpTcpBridge struct {
	api.PassThroughHttpTcpBridge

	callbacks api.HttpTcpBridgeCallbackHandler
	config    *config
}

func (f *httpTcpBridge) EncodeHeaders(headerMap api.RequestHeaderMap, dataForSet api.BufferInstance, endOfStream bool) api.HttpTcpBridgeStatus {
	if endOfStream {
		dataForSet.SetString(fmt.Sprintf("%s-http-to-tcp-encode_headers", Name))
		return api.HttpTcpBridgeEndStream
	}
	return api.HttpTcpBridgeContinue
}

func (f *httpTcpBridge) EncodeData(buffer api.BufferInstance, endOfStream bool) api.HttpTcpBridgeStatus {
	if buffer.Len() != 0 {
		if strings.Contains(buffer.String(), "local-reply") {
			buffer.SetString(fmt.Sprintf("%s-http-to-tcp-encode_data", Name))
			return api.HttpTcpBridgeEndStream
		}
	}
	return api.HttpTcpBridgeContinue
}

func (f *httpTcpBridge) OnUpstreamData(responseHeaderForSet api.ResponseHeaderMap, buffer api.BufferInstance, endOfStream bool) api.HttpTcpBridgeStatus {
	if !strings.Contains(buffer.String(), "end") {
		return api.HttpTcpBridgeStopAndBuffer
	}

	buffer.SetString(fmt.Sprintf("%s-tcp-to-http:%s", Name, buffer.String()))

	return api.HttpTcpBridgeEndStream
}

func (*httpTcpBridge) OnDestroy() {
	api.LogInfof("[OnDestroy] , httpTcpBridge destroy")
}
