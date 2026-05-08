package sdk

import (
	"fmt"

	"github.com/envoyproxy/envoy/source/extensions/dynamic_modules/sdk/go/shared"
)

// For built-in plugin factories in the host binary directly. DO NOT use this for independently
// compiled module or plugins.
var httpFilterConfigFactoryRegistry = make(map[string]shared.HttpFilterConfigFactory)
var networkFilterConfigFactoryRegistry = make(map[string]shared.NetworkFilterConfigFactory)

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

// NewNetworkFilterFactory creates a new network filter factory for the given plugin name and
// unparsed config.
func NewNetworkFilterFactory(handle shared.NetworkFilterConfigHandle, name string,
	unparsedConfig []byte) (shared.NetworkFilterFactory, error) {
	configFactory := networkFilterConfigFactoryRegistry[name]
	if configFactory == nil {
		return nil, fmt.Errorf("failed to get network filter config factory for %s", name)
	}
	return configFactory.Create(handle, unparsedConfig)
}

// GetNetworkFilterConfigFactory gets the network filter config factory for the given plugin name.
func GetNetworkFilterConfigFactory(name string) shared.NetworkFilterConfigFactory {
	return networkFilterConfigFactoryRegistry[name]
}

// RegisterNetworkFilterConfigFactories registers network filter config factories for plugins in the
// composer binary itself. This function MUST only be called from init() functions.
func RegisterNetworkFilterConfigFactories(factories map[string]shared.NetworkFilterConfigFactory) {
	for name, factory := range factories {
		if _, ok := networkFilterConfigFactoryRegistry[name]; ok {
			panic("network filter config factory already registered: " + name)
		}
		networkFilterConfigFactoryRegistry[name] = factory
	}
}
