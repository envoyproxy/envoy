package add_data

import (
	"google.golang.org/protobuf/types/known/anypb"

	"github.com/envoyproxy/envoy/contrib/golang/common/go/api"
	"github.com/envoyproxy/envoy/contrib/golang/filters/http/source/go/pkg/http"
)

const Name = "add_data"

func init() {
	http.RegisterHttpFilterFactoryAndConfigParser(Name, filterFactory, &parser{})
}

type config struct {
}

type parser struct {
}

func (p *parser) Parse(any *anypb.Any, callbacks api.ConfigCallbackHandler) (interface{}, error) {
	return &config{}, nil
}

func (p *parser) Merge(parent interface{}, child interface{}) interface{} {
	return child
}

func filterFactory(c interface{}, callbacks api.FilterCallbackHandler) api.StreamFilter {
	conf, ok := c.(*config)
	if !ok {
		panic("unexpected config type")
	}
	return &filter{
		callbacks: callbacks,
		config:    conf,
	}
}
