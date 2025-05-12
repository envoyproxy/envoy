package main

import (
	xds "github.com/cncf/xds/go/xds/type/v3"
	"github.com/envoyproxy/envoy/contrib/golang/router/cluster_specifier/source/go/pkg/api"
	"github.com/envoyproxy/envoy/contrib/golang/router/cluster_specifier/source/go/pkg/cluster_specifier"
	"google.golang.org/protobuf/types/known/anypb"
)

func init() {
	cluster_specifier.RegisterClusterSpecifierConfigFactory(configFactory)
}

func configFactory(config *anypb.Any) api.ClusterSpecifier {
	configStruct := &xds.TypedStruct{}
	if err := config.UnmarshalTo(configStruct); err != nil {
		panic(err)
	}
	plugin := &clusterSpecifier{}
	m := configStruct.Value.AsMap()
	if value, ok := m["invalid_prefix"]; ok {
		if valueStr, ok := value.(string); ok {
			plugin.invalidPrefix = valueStr
		}
	}
	if value, ok := m["default_prefix"]; ok {
		if valueStr, ok := value.(string); ok {
			plugin.defaultPrefix = valueStr
		}
	}
	if value, ok := m["panic_prefix"]; ok {
		if valueStr, ok := value.(string); ok {
			plugin.panicPrefix = valueStr
		}
	}
	return plugin
}

func main() {}
