package main

import (
	"google.golang.org/protobuf/types/known/anypb"

	"github.com/envoyproxy/envoy/contrib/golang/common/go/api"
	"github.com/envoyproxy/envoy/contrib/golang/filters/http/source/go/pkg/http"
)

const Name = "basic"

func init() {
	http.RegisterHttpFilterConfigFactoryAndParser(Name, ConfigFactory, &parser{})
}

type config struct {
	counter   api.Metric
	gauge     api.Metric
	histogram api.Metric
}

type parser struct {
}

func (p *parser) Parse(any *anypb.Any, callbacks api.ConfigCallbackHandler) (interface{}, error) {
	conf := &config{}
	if callbacks != nil {
	    conf.counter = callbacks.DefineMetric(0, "test-counter")
		conf.gauge = callbacks.DefineMetric(1, "test-gauge")
		conf.histogram = callbacks.DefineMetric(2, "test-histogram")
	}
	return conf, nil
}

func (p *parser) Merge(parent interface{}, child interface{}) interface{} {
	panic("TODO")
}

func ConfigFactory(c interface{}) api.StreamFilterFactory {
	conf, ok := c.(*config)
	if !ok {
		panic("unexpected config type")
	}
	return func(callbacks api.FilterCallbackHandler) api.StreamFilter {
		return &filter{
			callbacks: callbacks,
			config:    conf,
		}
	}
}

func main() {
}
