package main

import (
	xds "github.com/cncf/xds/go/xds/type/v3"
	"github.com/envoyproxy/envoy/contrib/golang/common/go/registry"
	"github.com/envoyproxy/envoy/contrib/golang/http/cluster_specifier/source/go/pkg/api"
	"google.golang.org/protobuf/types/known/anypb"
)

func init() {
	registry.RegisterClusterSpecifierFactory(configFactory)
	registry.RegisterClusterSpecifierConfigParser(&parser{})
}

type parser struct {
}

func (p *parser) Parse(config *anypb.Any) interface{} {
	configStruct := &xds.TypedStruct{}
	if err := config.UnmarshalTo(configStruct); err != nil {
		panic(err)
	}
	var conf pluginConfig
	if value, ok := configStruct.Value.AsMap()["invalid_prefix"]; ok {
		if valueStr, ok := value.(string); ok {
			conf.invalidPrefix = valueStr
		}
	}
	return &conf
}

func configFactory(config interface{}) api.ClusterSpecifier {
	conf, ok := config.(*pluginConfig)
	if !ok {
		panic("invalid config type")
	}
	return &clusterSpecifier{
		config: conf,
	}
}

func main() {}
