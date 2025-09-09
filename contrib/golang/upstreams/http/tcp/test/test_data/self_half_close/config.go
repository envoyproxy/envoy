package self_half_close

import (
	"google.golang.org/protobuf/types/known/anypb"

	"github.com/envoyproxy/envoy/contrib/golang/common/go/api"
	"github.com/envoyproxy/envoy/contrib/golang/upstreams/http/tcp/source/go/pkg/upstreams/http/tcp"
)

const Name = "self_half_close"

func init() {
	tcp.RegisterHttpTcpBridgeFactoryAndConfigParser(Name, filterFactory, &parser{})
}

type config struct {
}

type parser struct {
}

// Parse the filter configuration. We can call the ConfigCallbackHandler to control the filter's behavior
func (p *parser) Parse(any *anypb.Any) (interface{}, error) {
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
