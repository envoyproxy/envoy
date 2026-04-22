package sdk

import (
	"fmt"

	"github.com/envoyproxy/envoy/source/extensions/dynamic_modules/sdk/go/shared"
)

// For built-in plugin factories in the host binary directly. DO NOT use this for independently
// compiled module or plugins.
var httpFilterConfigFactoryRegistry = make(map[string]shared.HttpFilterConfigFactory)

// NewHttpFilterFactory creates a new plugin factory for the given plugin name and unparsed config.
func NewHttpFilterFactory(handle shared.HttpFilterConfigHandle, name string,
	unparsedConfig []byte) (shared.HttpFilterFactory, error) {
	configFactory := httpFilterConfigFactoryRegistry[name]
	if configFactory == nil {
		return nil, fmt.Errorf("failed to get plugin config factory")
	}
	return configFactory.Create(handle, unparsedConfig)
}

// GetHttpFilterConfigFactory gets the plugin config factory for the given plugin name.
func GetHttpFilterConfigFactory(name string) shared.HttpFilterConfigFactory {
	return httpFilterConfigFactoryRegistry[name]
}

// RegisterHttpFilterConfigFactories registers plugin config factories for plugins in the composer
// binary itself. This function MUST only be called from init() functions.
func RegisterHttpFilterConfigFactories(factories map[string]shared.HttpFilterConfigFactory) {
	for name, factory := range factories {
		if _, ok := httpFilterConfigFactoryRegistry[name]; ok {
			// Same plugin name should only be register once in same lib.
			panic("plugin config factory already registered: " + name)
		}
		httpFilterConfigFactoryRegistry[name] = factory
	}
}
