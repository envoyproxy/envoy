package simple

import (
    "github.com/envoyproxy/envoy/contrib/golang/filters/http/source/go/pkg/api"
)

const Name = "simple"

func ConfigFactory(interface{}) api.StreamFilterFactory {
    return func(callbacks api.FilterCallbackHandler) api.StreamFilter {
        return &filter{
            callbacks: callbacks,
        }
    }
}
