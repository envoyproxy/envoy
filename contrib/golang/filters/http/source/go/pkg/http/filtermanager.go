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

	"github.com/envoyproxy/envoy/contrib/golang/common/go/api"
	"google.golang.org/protobuf/types/known/anypb"
)

var httpFilterFactoryAndParser = sync.Map{}

type filterFactoryAndParser struct {
	filterFactory api.StreamFilterFactory
	configParser  api.StreamFilterConfigParser
}

// nullParser is a no-op implementation of the StreamFilterConfigParser interface.
type nullParser struct{}

// Parse does nothing, returns the input `any` as is.
func (p *nullParser) Parse(any *anypb.Any, callbacks api.ConfigCallbackHandler) (interface{}, error) {
	return any, nil
}

// Merge only uses the childConfig, ignore the parentConfig.
func (p *nullParser) Merge(parentConfig interface{}, childConfig interface{}) interface{} {
	return childConfig
}

var NullParser api.StreamFilterConfigParser = &nullParser{}

// RegisterHttpFilterFactoryAndConfigParser registers the http filter factory and config parser for the specified plugin.
// The factory and parser should not be nil.
// Use the NullParser if the plugin does not care about config.
func RegisterHttpFilterFactoryAndConfigParser(name string, factory api.StreamFilterFactory, parser api.StreamFilterConfigParser) {
	if factory == nil {
		panic("filter factory should not be nil")
	}
	if parser == nil {
		panic("config parser should not be nil")
	}
	httpFilterFactoryAndParser.Store(name, &filterFactoryAndParser{factory, parser})
}

func getHttpFilterFactoryAndConfig(name string, configId uint64) (api.StreamFilterFactory, interface{}) {
	config, ok := configCache.Load(configId)
	if !ok {
		panic(fmt.Sprintf("config not found, plugin: %s, configId: %d", name, configId))
	}

	if v, ok := httpFilterFactoryAndParser.Load(name); ok {
		return (v.(*filterFactoryAndParser)).filterFactory, config
	}

	api.LogErrorf("plugin %s not found, pass through by default", name)

	// return PassThroughFactory when no factory found
	return PassThroughFactory, config
}

func getHttpFilterConfigParser(name string) api.StreamFilterConfigParser {
	if v, ok := httpFilterFactoryAndParser.Load(name); ok {
		parser := (v.(*filterFactoryAndParser)).configParser
		if parser == nil {
			panic(fmt.Sprintf("config parser not found, plugin: %s", name))
		}
		return parser
	}
	// return NullParser when no parser found
	return NullParser
}
