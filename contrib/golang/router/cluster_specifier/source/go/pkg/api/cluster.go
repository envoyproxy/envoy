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

package api

import "google.golang.org/protobuf/types/known/anypb"

type ClusterSpecifierConfigParser interface {
	Parse(any *anypb.Any) interface{}
}

type ClusterSpecifier interface {
	// Cluster return the cluster name that will be used in the Envoy side,
	// Envoy will use the default_cluster in the plugin config when return an empty string, or panic happens.
	Cluster(RequestHeaderMap) string
}

type ClusterSpecifierFactory func(config interface{}) ClusterSpecifier

type ClusterSpecifierConfigFactory func(any *anypb.Any) ClusterSpecifier

type RequestHeaderMap interface {
	// Get value of key
	// If multiple values associated with this key, first one will be returned.
	Get(key string) (string, bool)
}
