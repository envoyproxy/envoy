package main

import (
	"github.com/envoyproxy/envoy/contrib/golang/common/go/api"
	"github.com/envoyproxy/envoy/contrib/golang/filters/network/source/go/pkg/network"
)

func init() {
	network.RegisterNetworkFilterConfigFactory("", simpleConfigFactory)
}

var simpleConfigFactory = &SimpleConfigFactory{}

type SimpleConfigFactory struct{}

func (f *SimpleConfigFactory) CreateFactoryFromConfig(config interface{}) network.FilterFactory {
	return &SimpleFilterFactory{}
}

type SimpleFilterFactory struct{}

func (f *SimpleFilterFactory) CreateFilter(cb api.ConnectionCallback) api.DownstreamFilter {
	return &SimpleFilter{}
}

type SimpleFilter struct {
	api.EmptyDownstreamFilter
}

func main() {
}
