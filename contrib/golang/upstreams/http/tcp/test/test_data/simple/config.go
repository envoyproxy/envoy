package main

import (
	"google.golang.org/protobuf/types/known/anypb"

	"github.com/envoyproxy/envoy/contrib/golang/common/go/api"
	"github.com/envoyproxy/envoy/contrib/golang/upstreams/http/tcp/source/go/pkg/upstreams/http/tcp"
)

const Name = "simple"

func init() {
	tcp.RegisterHttpTcpBridgeFactoryAndConfigParser(Name, filterFactory, &parser{})
}

type config struct {
	confVal string
}

type parser struct {
}

func (p *parser) Parse(any *anypb.Any) (interface{}, error) {
	// configStruct := &xds.TypedStruct{}
	// if err := any.UnmarshalTo(configStruct); err != nil {
	// 	return nil, err
	// }

	// v := configStruct.Value
	// conf, ok := v.AsMap()["conf_val"]
	// if !ok {
	// 	return nil, errors.New("missing conf_val")
	// }

	// c := &config{
	// 	confVal: conf.(string),
	// }

	// return c, nil

	return &config{}, nil
}

// Merge configuration from the inherited parent configuration
func (p *parser) Merge(parentConfig interface{}, childConfig interface{}) interface{} {
	return childConfig
}

func filterFactory(c interface{}, callbacks api.HttpTcpBridgeCallbackHandler) api.HttpTcpBridge {
	conf, ok := c.(*config)
	if !ok {
		panic("unexpected config type")
	}
	return &httpTcpBridge{
		callbacks: callbacks,
		config:    conf,
	}
}

func main() {}
