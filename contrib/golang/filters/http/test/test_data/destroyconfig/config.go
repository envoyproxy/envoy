package destroyconfig

/*
// ref https://github.com/golang/go/issues/25832

#cgo CFLAGS: -I../api
#cgo linux LDFLAGS: -Wl,-unresolved-symbols=ignore-all
#cgo darwin LDFLAGS: -Wl,-undefined,dynamic_lookup

#include <stdlib.h>
#include <string.h>

#include "destroyconfig.h"

*/
import "C"
import (
	"google.golang.org/protobuf/types/known/anypb"
	"unsafe"
	"github.com/envoyproxy/envoy/contrib/golang/common/go/api"
	"github.com/envoyproxy/envoy/contrib/golang/filters/http/source/go/pkg/http"
)

const Name = "destroyconfig"

func init() {
	http.SetHttpCAPI(&capi{})
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

type parser struct {
	api.StreamFilterConfigParser
}

type capi struct {
	api.HttpCAPI
}

func (c *capi) HttpConfigFinalize(_ unsafe.Pointer) {}

func (c *capi) HttpDefineMetric(cfg unsafe.Pointer, metricType api.MetricType, name string) uint32 {
	cfgPointer = cfg
	return 0
}

func (p *parser) Parse(_ *anypb.Any, cb api.ConfigCallbackHandler) (interface{}, error) {
	conf := &config{cb}
	return conf, nil
}
