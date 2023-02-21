package main

import (
	xds "github.com/cncf/xds/go/xds/type/v3"
	"github.com/envoyproxy/envoy/contrib/golang/common/go/registry"
	"github.com/envoyproxy/envoy/contrib/golang/http/cluster_specifier/source/go/pkg/api"
	"google.golang.org/protobuf/types/known/anypb"
)

func init() {
	registry.RegisterClusterSpecifierConfigFactory(configFactory)
}

func configFactory(config *anypb.Any) api.ClusterSpecifier {
	configStruct := &xds.TypedStruct{}
	if err := config.UnmarshalTo(configStruct); err != nil {
		panic(err)
	}
	plugin := &clusterSpecifier{}
	if value, ok := configStruct.Value.AsMap()["invalid_prefix"]; ok {
		if valueStr, ok := value.(string); ok {
			plugin.invalidPrefix = valueStr
		}
	}
	if value, ok := configStruct.Value.AsMap()["default_prefix"]; ok {
		if valueStr, ok := value.(string); ok {
			plugin.defaultPrefix = valueStr
		}
	}
	return plugin
}

func main() {}
