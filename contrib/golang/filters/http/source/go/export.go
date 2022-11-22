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

//go:linkname moeNewHttpPluginConfig github.com/envoyproxy/envoy/contrib/golang/filters/http/source/go/pkg/http.moeNewHttpPluginConfig
//export moeNewHttpPluginConfig
func moeNewHttpPluginConfig(configPtr uint64, configLen uint64) uint64

//go:linkname moeDestroyHttpPluginConfig github.com/envoyproxy/envoy/contrib/golang/filters/http/source/go/pkg/http.moeDestroyHttpPluginConfig
//export moeDestroyHttpPluginConfig
func moeDestroyHttpPluginConfig(id uint64)

//go:linkname moeMergeHttpPluginConfig github.com/envoyproxy/envoy/contrib/golang/filters/http/source/go/pkg/http.moeMergeHttpPluginConfig
//export moeMergeHttpPluginConfig
func moeMergeHttpPluginConfig(parentId uint64, childId uint64) uint64

//go:linkname moeOnHttpHeader github.com/envoyproxy/envoy/contrib/golang/filters/http/source/go/pkg/http.moeOnHttpHeader
//export moeOnHttpHeader
func moeOnHttpHeader(r *C.httpRequest, endStream, headerNum, headerBytes uint64) uint64

//go:linkname moeOnHttpData github.com/envoyproxy/envoy/contrib/golang/filters/http/source/go/pkg/http.moeOnHttpData
//export moeOnHttpData
func moeOnHttpData(r *C.httpRequest, endStream, buffer, length uint64) uint64

//go:linkname moeOnHttpDestroy github.com/envoyproxy/envoy/contrib/golang/filters/http/source/go/pkg/http.moeOnHttpDestroy
//export moeOnHttpDestroy
func moeOnHttpDestroy(r *C.httpRequest, reason uint64)
