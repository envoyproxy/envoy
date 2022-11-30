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

/*
// ref https://github.com/golang/go/issues/25832

#cgo CFLAGS: -Ipkg/api -Ivendor/github.com/envoyproxy/envoy/contrib/golang/filters/http/source/go/pkg/api
#cgo linux LDFLAGS: -Wl,-unresolved-symbols=ignore-all
#cgo darwin LDFLAGS: -Wl,-undefined,dynamic_lookup

#include <stdlib.h>
#include <string.h>

#include "api.h"

*/
import "C"

import (
    _ "github.com/envoyproxy/envoy/contrib/golang/filters/http/source/go/pkg/api"
    _ "github.com/envoyproxy/envoy/contrib/golang/filters/http/source/go/pkg/http"
)

//go:linkname envoyGoFilterNewHttpPluginConfig github.com/envoyproxy/envoy/contrib/golang/filters/http/source/go/pkg/http.envoyGoFilterNewHttpPluginConfig
//export envoyGoFilterNewHttpPluginConfig
func envoyGoFilterNewHttpPluginConfig(configPtr uint64, configLen uint64) uint64

//go:linkname envoyGoFilterDestroyHttpPluginConfig github.com/envoyproxy/envoy/contrib/golang/filters/http/source/go/pkg/http.envoyGoFilterDestroyHttpPluginConfig
//export envoyGoFilterDestroyHttpPluginConfig
func envoyGoFilterDestroyHttpPluginConfig(id uint64)

//go:linkname envoyGoFilterMergeHttpPluginConfig github.com/envoyproxy/envoy/contrib/golang/filters/http/source/go/pkg/http.envoyGoFilterMergeHttpPluginConfig
//export envoyGoFilterMergeHttpPluginConfig
func envoyGoFilterMergeHttpPluginConfig(parentId uint64, childId uint64) uint64

//go:linkname envoyGoFilterOnHttpHeader github.com/envoyproxy/envoy/contrib/golang/filters/http/source/go/pkg/http.envoyGoFilterOnHttpHeader
//export envoyGoFilterOnHttpHeader
func envoyGoFilterOnHttpHeader(r *C.httpRequest, endStream, headerNum, headerBytes uint64) uint64

//go:linkname envoyGoFilterOnHttpData github.com/envoyproxy/envoy/contrib/golang/filters/http/source/go/pkg/http.envoyGoFilterOnHttpData
//export envoyGoFilterOnHttpData
func envoyGoFilterOnHttpData(r *C.httpRequest, endStream, buffer, length uint64) uint64

//go:linkname envoyGoFilterOnHttpDestroy github.com/envoyproxy/envoy/contrib/golang/filters/http/source/go/pkg/http.envoyGoFilterOnHttpDestroy
//export envoyGoFilterOnHttpDestroy
func envoyGoFilterOnHttpDestroy(r *C.httpRequest, reason uint64)
