package main

import (
	"bytes"
	"errors"
	"fmt"

	xds "github.com/cncf/xds/go/xds/type/v3"
	"google.golang.org/protobuf/types/known/anypb"

	"github.com/envoyproxy/envoy/contrib/golang/common/go/api"
	"github.com/envoyproxy/envoy/contrib/golang/upstreams/http/tcp/source/go/pkg/upstreams/http/tcp"
)

const Name = "http2dubbo-by-golang-extension"

func init() {
	tcp.RegisterTcpUpstreamFactoryAndConfigParser(Name, filterFactory, &parser{})
}

type config struct {
	routerNameForGrayTraffic            string
	clusterNameForEnableRemoteHalfClose string
	enableTunneling                     bool
}

type parser struct {
}

// Parse the filter configuration. We can call the ConfigCallbackHandler to control the filter's behavior
func (p *parser) Parse(any *anypb.Any, callbacks api.ConfigCallbackHandler) (interface{}, error) {
	configStruct := &xds.TypedStruct{}
	if err := any.UnmarshalTo(configStruct); err != nil {
		return nil, err
	}

	v := configStruct.Value
	conf := &config{}

	routerName, ok := v.AsMap()["router_for_gray_traffic"]
	if !ok {
		return nil, errors.New("missing router_for_gray_traffic")
	}
	if routerNameStr, ok := routerName.(string); ok {
		conf.routerNameForGrayTraffic = routerNameStr
	} else {
		return nil, fmt.Errorf("router_for_gray_traffic: expect string while got %T", routerName)
	}

	clusterName, ok := v.AsMap()["cluster_for_enable_remote_half_close"]
	if !ok {
		return nil, errors.New("missing cluster_for_enable_remote_half_close")
	}
	if clusterNameStr, ok := clusterName.(string); ok {
		conf.clusterNameForEnableRemoteHalfClose = clusterNameStr
	} else {
		return nil, fmt.Errorf("cluster_for_enable_remote_half_close: expect string while got %T", clusterName)
	}

	enableTunneling, ok := v.AsMap()["enable_tunneling"]
	if !ok {
		return nil, errors.New("missing enable_tunneling")
	}
	if enableTunnelingBool, ok := enableTunneling.(bool); ok {
		conf.enableTunneling = enableTunnelingBool
	} else {
		return nil, fmt.Errorf("envoy_self_enable_half_close: expect bool while got %T", enableTunnelingBool)
	}

	return conf, nil
}

// Merge configuration from the inherited parent configuration
func (p *parser) Merge(parentConfig interface{}, childConfig interface{}) interface{} {
	return childConfig
}

func filterFactory(c interface{}, callbacks api.TcpUpstreamCallbackHandler) api.TcpUpstreamFilter {
	conf, ok := c.(*config)
	if !ok {
		panic("unexpected config type")
	}
	buf := make([]byte, 0)
	return &tcpUpstreamFilter{
		callbacks:      callbacks,
		config:         conf,
		httpReqBodyBuf: bytes.NewBuffer(buf),
	}
}

func main() {}
