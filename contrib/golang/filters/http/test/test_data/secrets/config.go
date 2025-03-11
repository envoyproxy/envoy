package secrets

import (
	"google.golang.org/protobuf/types/known/anypb"

	"github.com/envoyproxy/envoy/contrib/golang/common/go/api"
	"github.com/envoyproxy/envoy/contrib/golang/filters/http/source/go/pkg/http"
)

const Name = "secrets"

func init() {
	http.RegisterHttpFilterFactoryAndConfigParser(Name, filterFactory, &parser{})
}

type config struct {
	secretManager api.SecretManager
}

type parser struct {
}

func (p *parser) Parse(_ *anypb.Any, callbacks api.ConfigCallbackHandler) (interface{}, error) {
	conf := &config{
		secretManager: callbacks.SecretManager(),
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
		callbacks:     callbacks,
		secretManager: conf.secretManager,
	}
}
