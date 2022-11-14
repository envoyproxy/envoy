package simple

import (
	"github.com/envoyproxy/envoy/examples/golang/envoy-go-filter-samples/pkg/http/api"
)

func ConfigFactory(interface{}) api.HttpFilterFactory {
	return func(callbacks api.FilterCallbackHandler) api.HttpFilter {
		return &filter{
			callbacks: callbacks,
		}
	}
}
