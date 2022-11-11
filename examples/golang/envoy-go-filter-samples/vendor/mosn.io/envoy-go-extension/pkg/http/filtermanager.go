package http

import (
	"mosn.io/envoy-go-extension/pkg/http/api"
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

// streaming and async supported by default
func RegisterStreamingHttpFilterConfigFactory(f api.HttpFilterConfigFactory) {
	httpFilterConfigFactory = f
}
