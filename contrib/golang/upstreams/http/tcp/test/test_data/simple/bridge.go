package main

import (
	"fmt"

	"github.com/envoyproxy/envoy/contrib/golang/common/go/api"
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
}

func (f *httpTcpBridge) EncodeHeaders(headerMap api.RequestHeaderMap, dataForSet api.BufferInstance, endOfStream bool) api.HttpTcpBridgeStatus {

	dataForSet.SetString(fmt.Sprintf("conf_val-%s-to-tcp_data", f.config.confVal))

	return api.HttpTcpBridgeContinue
}

func (f *httpTcpBridge) EncodeData(buffer api.BufferInstance, endOfStream bool) api.HttpTcpBridgeStatus {

	return api.HttpTcpBridgeContinue
}

func (f *httpTcpBridge) OnUpstreamData(responseHeaderForSet api.ResponseHeaderMap, buffer api.BufferInstance, endOfStream bool) api.HttpTcpBridgeStatus {

	return api.HttpTcpBridgeContinue
}

func (*httpTcpBridge) OnDestroy() {
	api.LogInfof("[OnDestroy] , httpTcpBridge destroy")
}
