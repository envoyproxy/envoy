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

/*
// ref https://github.com/golang/go/issues/25832

#cgo CFLAGS: -I../../../../../../../../../../../common/go/api -I../api
#cgo linux LDFLAGS: -Wl,-unresolved-symbols=ignore-all
#cgo darwin LDFLAGS: -Wl,-undefined,dynamic_lookup

#include <stdlib.h>
#include <string.h>

#include "api.h"

*/
import "C"
import (
	"unsafe"

	"github.com/envoyproxy/envoy/contrib/golang/common/go/api"
)

type httpRequest struct {
	req           *C.httpRequest
	httpTcpBridge api.HttpTcpBridge

	decodingState processState
	encodingState processState
}

type processState struct {
	request      *httpRequest
	processState *C.processState
}

func (r *httpRequest) pluginName() string {
	return C.GoStringN(r.req.plugin_name.data, C.int(r.req.plugin_name.len))
}

func (r *httpRequest) GetRouteName() string {
	// in upstream stage, route has been determined, so ignore error.
	name, _ := cAPI.GetStringValue(unsafe.Pointer(r), ValueRouteName)
	return name
}
func (r *httpRequest) GetVirtualClusterName() string {
	// in upstream stage, cluster has been determined, so ignore error.
	name, _ := cAPI.GetStringValue(unsafe.Pointer(r), ValueClusterName)
	return name
}

func (r *httpRequest) SetSelfHalfCloseForUpstreamConn(enabled bool) {
	var enabledInt int
	if enabled {
		enabledInt = 1
	} else {
		enabledInt = 0
	}
	cAPI.SetSelfHalfCloseForUpstreamConn(unsafe.Pointer(r), enabledInt)
}
