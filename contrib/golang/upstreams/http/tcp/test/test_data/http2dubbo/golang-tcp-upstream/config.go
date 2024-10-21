package main

import (
	"errors"
	"fmt"

	xds "github.com/cncf/xds/go/xds/type/v3"
	"google.golang.org/protobuf/types/known/anypb"

	"github.com/envoyproxy/envoy/contrib/golang/common/go/api"
	"github.com/envoyproxy/envoy/contrib/golang/upstreams/http/tcp/source/go/pkg/upstreams/http/tcp"
)

const Name = "simple-tcp-upstream"

func init() {
	tcp.RegisterTcpUpstreamFactoryAndConfigParser(Name, filterFactory, &parser{})
}

type config struct {
	routerNameForEnableRemoteHalfClose string
	clusterNameForMock string
	envoySelfEnableHalfClose bool
}

type parser struct {
}

// Parse the filter configuration. We can call the ConfigCallbackHandler to control the filter's
// behavior
func (p *parser) Parse(any *anypb.Any, callbacks api.ConfigCallbackHandler) (interface{}, error) {
	configStruct := &xds.TypedStruct{}
	if err := any.UnmarshalTo(configStruct); err != nil {
		return nil, err
	}

	v := configStruct.Value
	conf := &config{}
	clusterName, ok := v.AsMap()["cluster_for_mock"]
	if !ok {
		return nil, errors.New("missing cluster_for_mock")
	}
	if clusterNameStr, ok := clusterName.(string); ok {
		conf.clusterNameForMock = clusterNameStr
	} else {
		return nil, fmt.Errorf("cluster_for_mock: expect string while got %T", clusterName)
	}

	routerName, ok := v.AsMap()["router_for_enable_remote_half_close"]
	if !ok {
		return nil, errors.New("missing router_for_enable_remote_half_close")
	}
	if routerNameStr, ok := routerName.(string); ok {
		conf.routerNameForEnableRemoteHalfClose = routerNameStr
	} else {
		return nil, fmt.Errorf("router_for_enable_remote_half_close: expect string while got %T", routerName)
	}

	envoySelfHalfClose, ok := v.AsMap()["envoy_self_enable_half_close"]
	if !ok {
		return nil, errors.New("missing envoy_self_enable_half_close")
	}
	if envoySelfHalfCloseBool, ok := envoySelfHalfClose.(bool); ok {
		conf.envoySelfEnableHalfClose = envoySelfHalfCloseBool
	} else {
		return nil, fmt.Errorf("envoy_self_enable_half_close: expect bool while got %T", envoySelfHalfCloseBool)
	}

	return conf, nil
}

func filterFactory(c interface{}, callbacks api.TcpUpstreamCallbackHandler) api.TcpUpstreamFilter {
	conf, ok := c.(*config)
	if !ok {
		panic("unexpected config type")
	}
	return &tcpUpstreamFilter{
		callbacks: callbacks,
		config:    conf,
	}
}

func main() {}
