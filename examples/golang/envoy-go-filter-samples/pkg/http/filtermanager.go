package http

import (
	"github.com/envoyproxy/envoy/examples/golang/envoy-go-filter-samples/pkg/http/api"
)

// pass through by default
var httpFilterConfigFactory api.HttpFilterConfigFactory = PassThroughFactory

// raw mode, sync by default
func RegisterHttpFilterConfigFactory(f api.HttpFilterConfigFactory) {
	httpFilterConfigFactory = f
}

func getOrCreateHttpFilterFactory(configId uint64) api.HttpFilterFactory {
	config, ok := configCache[configId]
	if !ok {
		// TODO: panic
	}
	return httpFilterConfigFactory(config)
}
