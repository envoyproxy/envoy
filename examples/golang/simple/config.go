package main

import (
	udpa "github.com/cncf/xds/go/udpa/type/v1"
	"github.com/envoyproxy/envoy/contrib/golang/filters/http/source/go/pkg/api"
	"github.com/envoyproxy/envoy/contrib/golang/filters/http/source/go/pkg/http"
	"google.golang.org/protobuf/types/known/anypb"
)

const Name = "simple"

func init() {
	http.RegisterHttpFilterConfigFactory(Name, ConfigFactory)
}

func ConfigFactory(config interface{}) api.StreamFilterFactory {
	any, ok := config.(*anypb.Any)
	if !ok {
		return nil
	}

	configStruct := &udpa.TypedStruct{}
	if err := any.UnmarshalTo(configStruct); err != nil {
		return nil
	}

	return func(callbacks api.FilterCallbackHandler) api.StreamFilter {
		return &filter{
			callbacks: callbacks,
			config:    configStruct.Value,
		}
	}
}

func main() {}
