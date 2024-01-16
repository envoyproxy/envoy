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

package cluster_specifier

/*
// ref https://github.com/golang/go/issues/25832

#cgo linux LDFLAGS: -Wl,-unresolved-symbols=ignore-all
#cgo darwin LDFLAGS: -Wl,-undefined,dynamic_lookup

#include <stdlib.h>
#include <string.h>

#include "api.h"
*/
import "C"
import (
	"unsafe"

	"github.com/envoyproxy/envoy/contrib/golang/router/cluster_specifier/source/go/pkg/api"
)

const foundHeaderValue = 1

type httpCApiImpl struct{}

func (c *httpCApiImpl) HttpGetHeader(headerPtr uint64, key *string, value *string) bool {
	found := C.envoyGoClusterSpecifierGetHeader(C.ulonglong(headerPtr), unsafe.Pointer(key), unsafe.Pointer(value))
	return int(found) == foundHeaderValue
}

func (c *httpCApiImpl) HttpLogError(pluginPtr uint64, msg *string) {
	C.envoyGoClusterSpecifierLogError(C.ulonglong(pluginPtr), unsafe.Pointer(msg))
}

var cAPI api.HttpCAPI = &httpCApiImpl{}

// SetHttpCAPI for mock cAPI
func SetHttpCAPI(api api.HttpCAPI) {
	cAPI = api
}
