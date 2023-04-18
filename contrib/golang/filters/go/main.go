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

package main

import (
	"unsafe"

	_ "github.com/envoyproxy/envoy/contrib/golang/filters/go/pkg/http"
	_ "github.com/envoyproxy/envoy/contrib/golang/filters/go/pkg/network"
)

//export envoyGoFilterOnNetworkFilterConfig
func envoyGoFilterOnNetworkFilterConfig(libraryIDPtr uint64, libraryIDLen uint64, configPtr uint64, configLen uint64) uint64 {
	return 0
}

//export envoyGoFilterOnDownstreamConnection
func envoyGoFilterOnDownstreamConnection(wrapper unsafe.Pointer, pluginNamePtr uint64, pluginNameLen uint64,
	configID uint64) uint64 {
	return 0
}

//export envoyGoFilterOnDownstreamData
func envoyGoFilterOnDownstreamData(wrapper unsafe.Pointer, dataSize uint64, dataPtr uint64, sliceNum int, endOfStream int) uint64 {
	return 0
}

//export envoyGoFilterOnDownstreamEvent
func envoyGoFilterOnDownstreamEvent(wrapper unsafe.Pointer, event int) {}

//export envoyGoFilterOnDownstreamWrite
func envoyGoFilterOnDownstreamWrite(wrapper unsafe.Pointer, dataSize uint64, dataPtr uint64, sliceNum int, endOfStream int) uint64 {
	return 0
}

//export envoyGoFilterOnUpstreamConnectionReady
func envoyGoFilterOnUpstreamConnectionReady(wrapper unsafe.Pointer) {}

//export envoyGoFilterOnUpstreamConnectionFailure
func envoyGoFilterOnUpstreamConnectionFailure(wrapper unsafe.Pointer, reason int) {}

//export envoyGoFilterOnUpstreamData
func envoyGoFilterOnUpstreamData(wrapper unsafe.Pointer, dataSize uint64, dataPtr uint64, sliceNum int, endOfStream int) {
}

//export envoyGoFilterOnUpstreamEvent
func envoyGoFilterOnUpstreamEvent(wrapper unsafe.Pointer, event int) {}

func main() {
}
