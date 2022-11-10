package simple

import (
	"mosn.io/envoy-go-extension/pkg/http/api"
)

func ConfigFactory(interface{}) api.HttpFilterFactory {
	return func(callbacks api.FilterCallbackHandler) api.HttpFilter {
		return &filter{
			callbacks: callbacks,
		}
	}
}
