// Upstream HTTP/TCP bridge test module. Mirrors test_data/rust/upstream_http_tcp_bridge.rs.
//
// Two modes selected by the config bytes:
//
//	"local_reply" — short-circuits with a 403 local reply on encode_headers.
//	anything else — streaming bridge: forwards request method as a "METHOD=X " prefix to
//	                the upstream, then forwards the request body, and converts upstream
//	                bytes back into HTTP response data.
//
// Loaded by test/extensions/upstreams/http/dynamic_modules/integration_test.cc which is
// parameterized over (rust, go).
package main

import (
	"fmt"

	sdk "github.com/envoyproxy/envoy/source/extensions/dynamic_modules/sdk/go"
	_ "github.com/envoyproxy/envoy/source/extensions/dynamic_modules/sdk/go/abi"
	"github.com/envoyproxy/envoy/source/extensions/dynamic_modules/sdk/go/shared"
)

func init() {
	sdk.RegisterUpstreamHttpTcpBridgeConfigFactories(map[string]shared.UpstreamHttpTcpBridgeConfigFactory{
		"test_bridge": &testBridgeConfigFactory{},
	})
}

func main() {} //nolint:all

type bridgeMode int

const (
	modeStreaming bridgeMode = iota
	modeLocalReply
)

type testBridgeConfigFactory struct{}

func (testBridgeConfigFactory) Create(_ string, config []byte) (shared.UpstreamHttpTcpBridgeFactory, error) {
	mode := modeStreaming
	if string(config) == "local_reply" {
		mode = modeLocalReply
	}
	return &testBridgeFactory{mode: mode}, nil
}

type testBridgeFactory struct {
	shared.EmptyUpstreamHttpTcpBridgeFactory
	mode bridgeMode
}

func (f *testBridgeFactory) Create(_ shared.UpstreamHttpTcpBridgeHandle) shared.UpstreamHttpTcpBridge {
	return &testBridge{mode: f.mode}
}

type testBridge struct {
	shared.EmptyUpstreamHttpTcpBridge
	mode                bridgeMode
	responseHeadersSent bool
}

func (b *testBridge) EncodeHeaders(handle shared.UpstreamHttpTcpBridgeHandle, _ bool) {
	switch b.mode {
	case modeStreaming:
		method, _, ok := handle.GetRequestHeader(":method", 0)
		if ok {
			prefix := fmt.Sprintf("METHOD=%s ", method.ToUnsafeString())
			handle.SendUpstreamData([]byte(prefix), false)
		}
	case modeLocalReply:
		handle.SendResponse(403, nil, []byte("access denied"))
	}
}

func (b *testBridge) EncodeData(handle shared.UpstreamHttpTcpBridgeHandle, endOfStream bool) {
	chunks := handle.GetRequestBuffer()
	if len(chunks) == 0 {
		if endOfStream {
			handle.SendUpstreamData(nil, true)
		}
		return
	}
	for i, chunk := range chunks {
		isLast := endOfStream && i == len(chunks)-1
		handle.SendUpstreamData(chunk.ToUnsafeBytes(), isLast)
	}
}

func (b *testBridge) EncodeTrailers(handle shared.UpstreamHttpTcpBridgeHandle) {
	handle.SendUpstreamData(nil, true)
}

func (b *testBridge) OnUpstreamData(handle shared.UpstreamHttpTcpBridgeHandle, endOfStream bool) {
	if !b.responseHeadersSent {
		handle.SendResponseHeaders(200,
			[][2]string{{"x-bridge-mode", "dynamic_module"}}, false)
		b.responseHeadersSent = true
	}
	chunks := handle.GetResponseBuffer()
	if len(chunks) == 0 {
		if endOfStream {
			handle.SendResponseData(nil, true)
		}
		return
	}
	for i, chunk := range chunks {
		isLast := endOfStream && i == len(chunks)-1
		handle.SendResponseData(chunk.ToUnsafeBytes(), isLast)
	}
}
