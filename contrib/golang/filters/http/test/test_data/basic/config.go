package main

import (
	"github.com/envoyproxy/envoy/contrib/golang/filters/go/pkg/api"
	"github.com/envoyproxy/envoy/contrib/golang/filters/go/pkg/http"
)

const Name = "basic"

func init() {
	http.RegisterHttpFilterConfigFactory(Name, ConfigFactory)
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
