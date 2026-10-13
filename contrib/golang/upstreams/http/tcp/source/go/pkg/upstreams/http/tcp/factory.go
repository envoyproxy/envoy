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

package tcp

import (
	"fmt"
	"sync"

	"google.golang.org/protobuf/types/known/anypb"

	"github.com/envoyproxy/envoy/contrib/golang/common/go/api"
)

var httpTcpBridgeactoryAndParser = sync.Map{}

type filterFactoryAndParser struct {
	filterFactory api.HttpTcpBridgeFactory
	configParser  api.HttpTcpBridgeConfigParser
}

// nullParser is a no-op implementation of the HttpTcpBridgeConfigParser interface.
type nullParser struct{}

// Parse does nothing, returns the input `any` as is.
func (p *nullParser) Parse(any *anypb.Any) (interface{}, error) {
	return any, nil
}

var NullParser api.HttpTcpBridgeConfigParser = &nullParser{}

// RegisterHttpTcpBridgeFactoryAndConfigParser registers the http tcp bridge factory and config parser for the specified plugin.
// The factory and parser should not be nil.
// Use the NullParser if the plugin does not care about config.
func RegisterHttpTcpBridgeFactoryAndConfigParser(name string, factory api.HttpTcpBridgeFactory, parser api.HttpTcpBridgeConfigParser) {
	if factory == nil {
		panic("go side: golang http-tcp bridge: factory should not be nil")
	}
	if parser == nil {
		panic("go side: golang http-tcp bridge: config parser should not be nil")
	}
	httpTcpBridgeactoryAndParser.Store(name, &filterFactoryAndParser{factory, parser})
}

func getHttpTcpBridgeFactoryAndConfig(name string, configId uint64) (api.HttpTcpBridgeFactory, interface{}) {
	config, ok := configCache.Load(configId)
	if !ok {
		panic(fmt.Sprintf("go side: golang http-tcp bridge: config not found, plugin: %s, configId: %d", name, configId))
	}

	if v, ok := httpTcpBridgeactoryAndParser.Load(name); ok {
		return (v.(*filterFactoryAndParser)).filterFactory, config
	}

	api.LogErrorf("go side: golang http-tcp bridge: plugin %s not found, pass through by default", name)

	// return PassThroughFactory when no factory found
	return PassThroughFactory, config
}

func getHttpTcpBridgeConfigParser(name string) api.HttpTcpBridgeConfigParser {
	if v, ok := httpTcpBridgeactoryAndParser.Load(name); ok {
		parser := (v.(*filterFactoryAndParser)).configParser
		if parser == nil {
			panic(fmt.Sprintf("go side: golang http-tcp bridge: config parser not found, plugin: %s", name))
		}
		return parser
	}
	// return NullParser when no parser found
	return NullParser
}
