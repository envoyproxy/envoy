package main

import (
	"github.com/envoyproxy/envoy/contrib/golang/common/go/api"
	"github.com/envoyproxy/envoy/contrib/golang/filters/http/source/go/pkg/http"
)

const Name = "action"

func init() {
	http.RegisterHttpFilterConfigFactoryAndParser(Name, ConfigFactory, nil)
}

type config struct {
	decodeHeadersRet api.StatusType
}

func ConfigFactory(c interface{}) api.StreamFilterFactory {
	return func(callbacks api.FilterCallbackHandler) api.StreamFilter {
		return &filter{
			callbacks: callbacks,
		}
	}
}

func main() {}
