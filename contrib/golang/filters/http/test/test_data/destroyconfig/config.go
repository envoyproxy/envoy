package destroyconfig

/*
#cgo linux LDFLAGS: -Wl,-unresolved-symbols=ignore-all
#cgo darwin LDFLAGS: -Wl,-undefined,dynamic_lookup

#include "destroyconfig.h"

*/
import "C"
import (
	"github.com/envoyproxy/envoy/contrib/golang/common/go/api"
	"github.com/envoyproxy/envoy/contrib/golang/filters/http/source/go/pkg/http"
	"google.golang.org/protobuf/types/known/anypb"
	"unsafe"
)

const Name = "destroyconfig"

func init() {
	http.RegisterHttpFilterFactoryAndConfigParser(Name, http.PassThroughFactory, &parser{})
}

var cfgPointer unsafe.Pointer

type config struct {
	cb api.ConfigCallbackHandler
}

func (c *config) Destroy() {
	// call cApi.HttpDefineMetric to store the config pointer
	c.cb.DefineCounterMetric("")
	C.envoyGoConfigDestroy(cfgPointer)
}

type capi struct {
	api.HttpCAPI
}

func (c *capi) HttpConfigFinalize(_ unsafe.Pointer) {}

func (c *capi) HttpDefineMetric(cfg unsafe.Pointer, _ api.MetricType, _ string) uint32 {
	cfgPointer = cfg
	return 0
}

type parser struct {
	api.StreamFilterConfigParser
}

func (p *parser) Parse(_ *anypb.Any, cb api.ConfigCallbackHandler) (interface{}, error) {
	http.SetHttpCAPI(&capi{})
	conf := &config{cb}
	return conf, nil
}
