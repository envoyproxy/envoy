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

type SimpleFilter struct{}

func (f *SimpleFilter) OnNewConnection() api.FilterStatus {
	panic("implement me")
}

func (f *SimpleFilter) OnData(buffer []byte, endOfStream bool) api.FilterStatus {
	panic("implement me")
}

func (f *SimpleFilter) OnEvent(event api.ConnectionEvent) {
	panic("implement me")
}

func (f *SimpleFilter) OnWrite(buffer []byte, endOfStream bool) api.FilterStatus {
	panic("implement me")
}

func main() {
}
