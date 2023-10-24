/*
 * Licensed to the Apache Software Foundation (ASF) under one or more
 * contributor license agreements.  See the NOTICE file distributed with
 * this work for additional information regarding copyright ownership.
 * The ASF licenses this file to You under the Apache License, Version 2.0
 * (the "License"); you may not use this file except in compliance with
 * the License.  You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

package http

import (
	"fmt"
	"sync"

	"google.golang.org/protobuf/types/known/anypb"

	"github.com/envoyproxy/envoy/contrib/golang/common/go/api"
)

var httpFilterConfigFactoryAndParser = sync.Map{}
var httpManagedFilterConfigFactoryAndParser = sync.Map{}

type filterConfigFactoryAndParser struct {
	configFactory api.StreamFilterConfigFactory
	configParser  api.StreamFilterConfigParser
}

type managedFilterConfigFactoryAndParser struct {
	filterConfigFactoryAndParser
	configFactory api.StreamManagedFilterConfigFactory
}

// Register config factory and config parser for the specified plugin.
// The "factory" parameter is required, should not be nil,
// and the "parser" parameter is optional, could be nil.
func RegisterHttpFilterConfigFactoryAndParser(name string, factory api.StreamFilterConfigFactory, parser api.StreamFilterConfigParser) {
	if factory == nil {
		panic("config factory should not be nil")
	}
	httpFilterConfigFactoryAndParser.Store(name, &filterConfigFactoryAndParser{factory, parser})
}

func RegisterHttpManagedFilterConfigFactoryAndParser(name string, factory api.StreamManagedFilterConfigFactory, parser api.StreamFilterConfigParser) {
	if factory == nil {
		panic("config factory should not be nil")
	}
	httpManagedFilterConfigFactoryAndParser.Store(name, &managedFilterConfigFactoryAndParser{
		filterConfigFactoryAndParser{nil, parser},
		factory,
	})
}

func getOrCreateHttpFilterFactory(name string, configId uint64) api.StreamFilterFactory {
	config, ok := configCache.Load(configId)
	if !ok {
		panic(fmt.Sprintf("config not found, plugin: %s, configId: %d", name, configId))
	}

	if v, ok := httpFilterConfigFactoryAndParser.Load(name); ok {
		return (v.(*filterConfigFactoryAndParser)).configFactory(config)
	}

	api.LogErrorf("plugin %s not found, pass through by default", name)

	// pass through by default
	return PassThroughFactory(config)
}

func getHttpFilterConfigParser(name string) api.StreamFilterConfigParser {
	if v, ok := httpFilterConfigFactoryAndParser.Load(name); ok {
		return (v.(*filterConfigFactoryAndParser)).configParser
	}
	return nil
}

type filterManagerConfigParser struct {
}

type filterConfig struct {
	Name         string
	parsedConfig interface{}
}

type filterManagerConfig struct {
	current []*filterConfig
}

func (p *filterManagerConfigParser) Parse(any *anypb.Any, callbacks api.ConfigCallbackHandler) (interface{}, error) {
	configStruct := &FilterManagerConfig{}

	// No configuration
	if any.GetTypeUrl() == "" {
		conf := &filterManagerConfig{
			current: []*filterConfig{},
		}
		return conf, nil
	}

	if err := any.UnmarshalTo(configStruct); err != nil {
		return nil, err
	}

	protos := configStruct.GetConfigs()
	conf := &filterManagerConfig{
		current: make([]*filterConfig, 0, len(protos)),
	}
	for _, proto := range protos {
		name := proto.Name
		if v, ok := httpManagedFilterConfigFactoryAndParser.Load(name); ok {
			plugin := v.(*managedFilterConfigFactoryAndParser)
			config, err := plugin.configParser.Parse(proto.Config, nil)
			if err != nil {
				return nil, fmt.Errorf("%w during parsing plugin %s in filtermanager", err, name)
			}

			conf.current = append(conf.current, &filterConfig{
				Name:         proto.Name,
				parsedConfig: config,
			})
		} else {
			api.LogErrorf("plugin %s not found, ignored", name)
		}
	}

	return conf, nil
}

func (p *filterManagerConfigParser) Merge(parent interface{}, child interface{}) interface{} {
	// TODO: We have considered to implemented a Merge Policy between the LDS's filter & RDS's per route
	// config. A thought is to reuse the current Merge method. For example, considering we have
	// LDS:
	//	 - name: A
	//	   pet: cat
	// RDS:
	//	 - name: A
	//	   pet: dog
	// we will call plugin A's Merge method, which will produce `pet: [cat, dog]` or `pet: dog`.
	// As there is no real world use case for the Merge feature, I decide to delay its implementation
	// to avoid premature design.
	return child
}

type filterManager struct {
	filters []api.StreamFilter
	names   []string

	callbacks api.FilterCallbackHandler
}

func filterManagerConfigFactory(c interface{}) api.StreamFilterFactory {
	conf, ok := c.(*filterManagerConfig)
	if !ok {
		panic("unexpected config type")
	}

	newConfig := conf.current
	factories := make([]api.StreamManagedFilterFactory, len(newConfig))
	names := make([]string, len(factories))
	for i, fc := range newConfig {
		var factory api.StreamManagedFilterConfigFactory
		name := fc.Name
		names[i] = name
		if v, ok := httpManagedFilterConfigFactoryAndParser.Load(name); ok {
			plugin := v.(*managedFilterConfigFactoryAndParser)
			factory = plugin.configFactory
			config := fc.parsedConfig
			factories[i] = factory(config)

		} else {
			api.LogErrorf("plugin %s not found, pass through by default", name)
			factory = ManagedPassThroughFactory
			factories[i] = factory(nil)
		}
	}

	return func(callbacks api.FilterCallbackHandler) api.StreamFilter {
		filters := make([]api.StreamFilter, len(factories))
		for i, factory := range factories {
			filters[i] = factory(callbacks)
		}
		return &filterManager{
			callbacks: callbacks,
			names:     names,
			filters:   filters,
		}
	}
}

func (m *filterManager) DecodeHeaders(header api.RequestHeaderMap, endStream bool) api.StatusType {
	go func() {
		defer m.callbacks.RecoverPanic()

		for _, f := range m.filters {
			// When the filter is controlled by filterManager, it don't
			// need to create goroutine by itself. So there is no need
			// to return api.Running and resume inside the filter. We
			// treat api.Running as api.Continue here.
			status := f.DecodeHeaders(header, endStream)
			if status == api.LocalReply {
				return
			}
		}
		m.callbacks.Continue(api.Continue)
	}()

	return api.Running
}

func (m *filterManager) notifyUnsupportedStatus(name string, status api.StatusType) (willReturn bool) {
	if status == api.Running || status == api.Continue {
		return false
	}

	// status == StopXXX
	if len(m.filters) == 1 {
		// just use the status as the final result
		m.callbacks.Continue(status)
		return true
	}
	// If we have multiple filters, we can't implement the StopXXX semantics without
	// managing the buffer at Go's side. For example, considering we have two filters:
	// - Compress (return Continue to process streamingly)
	// - Rewrite (return StopAndBuffer to process the whole data)
	//
	// If we return StopAndBuffer as final result, the next time filter Compress will get the
	// whole data, which is unwanted.
	// However, managing the buffer at Go's side is inefficient, which will double the memory
	// usage at least.

	m.callbacks.Log(api.Error,
		fmt.Sprintf("unsupported status %s returned from filter %s, ignored", status.String(), name))
	return false
}

func (m *filterManager) DecodeData(buf api.BufferInstance, endStream bool) api.StatusType {
	go func() {
		defer m.callbacks.RecoverPanic()

		for i, f := range m.filters {
			status := f.DecodeData(buf, endStream)
			if status == api.LocalReply {
				return
			}

			if m.notifyUnsupportedStatus(m.names[i], status) {
				return
			}
		}
		m.callbacks.Continue(api.Continue)
	}()

	return api.Running
}

func (m *filterManager) DecodeTrailers(trailer api.RequestTrailerMap) api.StatusType {
	go func() {
		defer m.callbacks.RecoverPanic()

		for _, f := range m.filters {
			status := f.DecodeTrailers(trailer)
			if status == api.LocalReply {
				return
			}
		}
		m.callbacks.Continue(api.Continue)
	}()

	return api.Running
}

func (m *filterManager) EncodeHeaders(header api.ResponseHeaderMap, endStream bool) api.StatusType {
	go func() {
		defer m.callbacks.RecoverPanic()

		for _, f := range m.filters {
			status := f.EncodeHeaders(header, endStream)
			if status == api.LocalReply {
				return
			}
		}
		m.callbacks.Continue(api.Continue)
	}()

	return api.Running
}

func (m *filterManager) EncodeData(buf api.BufferInstance, endStream bool) api.StatusType {
	go func() {
		defer m.callbacks.RecoverPanic()

		for i, f := range m.filters {
			status := f.EncodeData(buf, endStream)
			if m.notifyUnsupportedStatus(m.names[i], status) {
				return
			}
		}
		m.callbacks.Continue(api.Continue)
	}()

	return api.Running
}

func (m *filterManager) EncodeTrailers(trailer api.ResponseTrailerMap) api.StatusType {
	go func() {
		defer m.callbacks.RecoverPanic()

		for _, f := range m.filters {
			f.EncodeTrailers(trailer)
		}
		m.callbacks.Continue(api.Continue)
	}()

	return api.Running
}

func (m *filterManager) OnLog() {
	for _, f := range m.filters {
		f.OnLog()
	}
}

func (m *filterManager) OnLogDownstreamStart() {
	for _, f := range m.filters {
		f.OnLogDownstreamStart()
	}
}

func (m *filterManager) OnLogDownstreamPeriodic() {
	for _, f := range m.filters {
		f.OnLogDownstreamPeriodic()
	}
}

func (m *filterManager) OnDestroy(reason api.DestroyReason) {
	for _, f := range m.filters {
		f.OnDestroy(reason)
	}
}

func RegisterHttpFilterManager(name string) {
	RegisterHttpFilterConfigFactoryAndParser(name, filterManagerConfigFactory, &filterManagerConfigParser{})
}
