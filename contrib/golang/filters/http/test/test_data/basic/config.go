package main

import (
	"github.com/envoyproxy/envoy/contrib/golang/common/go/api"
	"github.com/envoyproxy/envoy/contrib/golang/filters/http/source/go/pkg/http"
)

const Name = "basic"

func init() {
	api.LogCritical("init")
	api.LogCritical(api.GetLogLevel().String())

	http.RegisterHttpFilterConfigFactoryAndParser(Name, ConfigFactory, nil)
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
