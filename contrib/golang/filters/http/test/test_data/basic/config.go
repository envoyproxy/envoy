package main

import (
	"github.com/envoyproxy/envoy/contrib/golang/common/go/registry"
	"github.com/envoyproxy/envoy/contrib/golang/filters/http/source/go/pkg/api"
)

const Name = "basic"

func init() {
	registry.RegisterHttpFilterConfigFactory(Name, ConfigFactory)
}

func ConfigFactory(interface{}) api.StreamFilterFactory {
	return func(callbacks api.FilterCallbackHandler) api.StreamFilter {
		return &filter{
			callbacks: callbacks,
		}
	}
}

func main() {
}
