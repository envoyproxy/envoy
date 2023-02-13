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
	// TODO: support header
	// Choose(RequestHeaderMap) string
	Choose() string
}

type ClusterSpecifierFactory func(config interface{}) ClusterSpecifier

type HeaderMap interface {
	// Get value of key
	// If multiple values associated with this key, first one will be returned.
	Get(key string) (string, bool)

	// Set key-value pair in header map, the previous pair will be replaced if exists
	Set(key, value string)

	// Add value for given key.
	// Multiple headers with the same key may be added with this function.
	// Use Set for setting a single header for the given key.
	Add(key, value string)

	// Del delete pair of specified key
	Del(key string)

	// ByteSize return size of HeaderMap
	ByteSize() uint64
}

type RequestHeaderMap interface {
	HeaderMap
	Protocol() string
	Scheme() string
	Method() string
	Host() string
	Path() string

	// GetRaw is unsafe, reuse the memory from Envoy
	GetRaw(name string) string
}
