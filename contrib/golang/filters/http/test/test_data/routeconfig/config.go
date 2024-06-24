package main

import (
	"errors"

	xds "github.com/cncf/xds/go/xds/type/v3"
	"google.golang.org/protobuf/types/known/anypb"

	"github.com/envoyproxy/envoy/contrib/golang/common/go/api"
	"github.com/envoyproxy/envoy/contrib/golang/filters/http/source/go/pkg/http"
)

const Name = "routeconfig"

func init() {
	http.RegisterHttpFilterFactoryAndConfigParser(Name, filterFactory, &parser{})
}

func filterFactory(c interface{}, callbacks api.FilterCallbackHandler) api.StreamFilter {
	conf, ok := c.(*config)
	if !ok {
		panic("unexpected config type")
	}
	return &filter{
		config:    conf,
		callbacks: callbacks,
	}
}

type config struct {
	removeHeader string
	setHeader    string
}

type parser struct {
}

func (p *parser) Parse(any *anypb.Any, callbacks api.ConfigCallbackHandler) (interface{}, error) {
	configStruct := &xds.TypedStruct{}
	if err := any.UnmarshalTo(configStruct); err != nil {
		return nil, err
	}

	conf := &config{}
	m := configStruct.Value.AsMap()
	if _, ok := m["invalid"].(string); ok {
		return nil, errors.New("testing invalid config")
	}
	if remove, ok := m["remove"].(string); ok {
		conf.removeHeader = remove
	}
	if set, ok := m["set"].(string); ok {
		conf.setHeader = set
	}
	return conf, nil
}

func (p *parser) Merge(parent interface{}, child interface{}) interface{} {
	parentConfig := parent.(*config)
	childConfig := child.(*config)

	// copy one
	newConfig := *parentConfig
	if childConfig.removeHeader != "" {
		newConfig.removeHeader = childConfig.removeHeader
	}
	if childConfig.setHeader != "" {
		newConfig.setHeader = childConfig.setHeader
	}
	return &newConfig
}

func main() {
}
