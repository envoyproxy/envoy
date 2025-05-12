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

package network

import (
	"sync"

	"google.golang.org/protobuf/types/known/anypb"

	"github.com/envoyproxy/envoy/contrib/golang/common/go/api"
)

type ConfigFactory interface {
	CreateFactoryFromConfig(config interface{}) FilterFactory
}

type FilterFactory interface {
	CreateFilter(cb api.ConnectionCallback) api.DownstreamFilter
}

type ConfigParser interface {
	// TODO: should return error when the config is invalid.
	ParseConfig(any *anypb.Any) interface{}
}

var (
	networkFilterConfigFactoryMap = &sync.Map{} // pluginName -> ConfigFactory
)

func RegisterNetworkFilterConfigFactory(name string, factory ConfigFactory) {
	if factory != nil {
		networkFilterConfigFactoryMap.Store(name, factory)
	}
}

func GetNetworkFilterConfigFactory(name string) ConfigFactory {
	if v, ok := networkFilterConfigFactoryMap.Load(name); ok {
		return v.(ConfigFactory)
	}
	return nil
}

var networkFilterConfigParser ConfigParser = &noopConfigParser{}

func RegisterNetworkFilterConfigParser(parser ConfigParser) {
	if parser != nil {
		networkFilterConfigParser = parser
	}
}

func GetNetworkFilterConfigParser() ConfigParser {
	return networkFilterConfigParser
}

type noopConfigParser struct{}

var _ ConfigParser = (*noopConfigParser)(nil)

func (n *noopConfigParser) ParseConfig(any *anypb.Any) interface{} {
	return any
}
