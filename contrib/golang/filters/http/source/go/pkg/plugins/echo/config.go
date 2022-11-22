package echo

import (
    "github.com/envoyproxy/envoy/contrib/golang/filters/http/source/go/pkg/api"
)

const Name = "echo"

func ConfigFactory(interface{}) api.StreamFilterFactory {
    return func(callbacks api.FilterCallbackHandler) api.StreamFilter {
        return &filter{
            callbacks: callbacks,
        }
    }
}
