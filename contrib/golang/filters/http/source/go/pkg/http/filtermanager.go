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

	"github.com/envoyproxy/envoy/contrib/golang/filters/http/source/go/pkg/api"
)

var httpFilterConfigFactory = sync.Map{}

func RegisterHttpFilterConfigFactory(name string, f api.StreamFilterConfigFactory) {
	httpFilterConfigFactory.Store(name, f)
}

// no parser by default
var httpFilterConfigParser api.StreamFilterConfigParser = nil

// TODO merge it to api.HttpFilterConfigFactory
func RegisterHttpFilterConfigParser(parser api.StreamFilterConfigParser) {
	httpFilterConfigParser = parser
}

func getOrCreateHttpFilterFactory(name string, configId uint64) api.StreamFilterFactory {
	config, ok := configCache.Load(configId)
	if !ok {
		panic(fmt.Sprintf("get config failed, plugin: %s, configId: %d", name, configId))
	}

	if v, ok := httpFilterConfigFactory.Load(name); ok {
		return (v.(api.StreamFilterConfigFactory))(config)
	}

	// pass through by default
	return PassThroughFactory(config)
}

// streaming and async supported by default
func RegisterStreamingHttpFilterConfigFactory(name string, f api.StreamFilterConfigFactory) {
	httpFilterConfigFactory.Store(name, f)
}
