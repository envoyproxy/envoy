package secrets

import (
	xds "github.com/cncf/xds/go/xds/type/v3"
	"google.golang.org/protobuf/types/known/anypb"

	"github.com/envoyproxy/envoy/contrib/golang/common/go/api"
	"github.com/envoyproxy/envoy/contrib/golang/filters/http/source/go/pkg/http"
)

const Name = "secrets"

func init() {
	http.RegisterHttpFilterFactoryAndConfigParser(Name, filterFactory, &parser{})
}

type config struct {
	secretKey   string
	secretValue string
	path        string
}

type parser struct {
}

func (p *parser) Parse(any *anypb.Any, callbacks api.ConfigCallbackHandler) (interface{}, error) {
	configStruct := &xds.TypedStruct{}
	if err := any.UnmarshalTo(configStruct); err != nil {
		return nil, err
	}
	v := configStruct.Value
	path := v.AsMap()["path"].(string)
	secretKey := v.AsMap()["secret_key"].(string)
	secretValue, ok := v.AsMap()["secret_value"].(string)
	if !ok {
		secretValue = ""
	}
	conf := &config{
		secretKey:   secretKey,
		secretValue: secretValue,
		path:        path,
	}
	return conf, nil
}

func (p *parser) Merge(_ interface{}, _ interface{}) interface{} {
	panic("TODO")
}

func filterFactory(c interface{}, callbacks api.FilterCallbackHandler) api.StreamFilter {
	conf, ok := c.(*config)
	if !ok {
		panic("unexpected config type")
	}
	return &filter{
		callbacks: callbacks,
		config:    conf,
	}
}
