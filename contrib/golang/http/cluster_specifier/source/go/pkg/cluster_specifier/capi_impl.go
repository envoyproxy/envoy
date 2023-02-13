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

#cgo CFLAGS: -I../api
#cgo linux LDFLAGS: -Wl,-unresolved-symbols=ignore-all
#cgo darwin LDFLAGS: -Wl,-undefined,dynamic_lookup

#include <stdlib.h>
#include <string.h>

#include "api.h"

*/
import "C"
import (
	"reflect"
	"runtime"
	"unsafe"

	"github.com/envoyproxy/envoy/contrib/golang/http/cluster_specifier/source/go/pkg/api"
)

type httpCApiImpl struct{}

func (c *httpCApiImpl) HttpGetHeader(headerPtr uint64, key *string, value *string) {
	C.envoyGoClusterSpecifierGetHeader(headerPtr, unsafe.Pointer(key), unsafe.Pointer(value))
}

func (c *httpCApiImpl) HttpCopyHeaders(headerPtr uint64, num uint64, bytes uint64) map[string]string {
	// TODO: use a memory pool for better performance,
	// since these go strings in strs, will be copied into the following map.
	strs := make([]string, num*2)
	// but, this buffer can not be reused safely,
	// since strings may refer to this buffer as string data, and string is const in go.
	// we have to make sure the all strings is not using before reusing,
	// but strings may be alive beyond the request life.
	buf := make([]byte, bytes)
	sHeader := (*reflect.SliceHeader)(unsafe.Pointer(&strs))
	bHeader := (*reflect.SliceHeader)(unsafe.Pointer(&buf))

	C.envoyGoClusterSpecifierCopyHeaders(headerPtr, unsafe.Pointer(sHeader.Data), unsafe.Pointer(bHeader.Data))

	m := make(map[string]string, num)
	for i := uint64(0); i < num*2; i += 2 {
		key := strs[i]
		value := strs[i+1]
		m[key] = value
	}
	runtime.KeepAlive(buf)
	return m
}

func (c *httpCApiImpl) HttpSetHeader(headerPtr uint64, key *string, value *string) {
	C.envoyGoClusterSpecifierSetHeader(headerPtr, unsafe.Pointer(key), unsafe.Pointer(value))
}

func (c *httpCApiImpl) HttpRemoveHeader(headerPtr uint64, key *string) {
	C.envoyGoClusterSpecifierRemoveHeader(headerPtr, unsafe.Pointer(key))
}

var cAPI api.HttpCAPI = &httpCApiImpl{}

// SetHttpCAPI for mock cAPI
func SetHttpCAPI(api api.HttpCAPI) {
	cAPI = api
}
