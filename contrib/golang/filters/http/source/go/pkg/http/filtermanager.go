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
)

var httpFilterConfigFactoryAndParser = sync.Map{}

type filterConfigFactoryAndParser struct {
	configFactory api.StreamFilterConfigFactory
	configParser  api.StreamFilterConfigParser
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
