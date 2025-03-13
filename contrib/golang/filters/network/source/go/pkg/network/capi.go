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

/*
// ref https://github.com/golang/go/issues/25832

#cgo CFLAGS: -I../../../../../../common/go/api -I../api
#cgo linux LDFLAGS: -Wl,-unresolved-symbols=ignore-all
#cgo darwin LDFLAGS: -Wl,-undefined,dynamic_lookup

#include <stdlib.h>
#include <string.h>

#include "api.h"

*/
import "C"
import (
	"strings"
	"sync/atomic"
	"unsafe"

	"github.com/envoyproxy/envoy/contrib/golang/common/go/api"
)

var cgoAPI api.NetworkCAPI = &cgoApiImpl{}

func SetCgoAPI(apiImpl api.NetworkCAPI) {
	if apiImpl != nil {
		cgoAPI = apiImpl
	}
}

type cgoApiImpl struct{}

func (c *cgoApiImpl) DownstreamWrite(f unsafe.Pointer, bufferPtr unsafe.Pointer, bufferLen int, endStream int) {
	C.envoyGoFilterDownstreamWrite(f, bufferPtr, C.int(bufferLen), C.int(endStream))
}

func (c *cgoApiImpl) DownstreamClose(f unsafe.Pointer, closeType int) {
	C.envoyGoFilterDownstreamClose(f, C.int(closeType))
}

func (c *cgoApiImpl) DownstreamFinalize(f unsafe.Pointer, reason int) {
	C.envoyGoFilterDownstreamFinalize(f, C.int(reason))
}

func (c *cgoApiImpl) DownstreamInfo(f unsafe.Pointer, infoType int) string {
	var info string
	C.envoyGoFilterDownstreamInfo(f, C.int(infoType), unsafe.Pointer(&info))
	return strings.Clone(info)
}

func (c *cgoApiImpl) GetFilterState(f unsafe.Pointer, key string) string {
	cb := (*connectionCallback)(f)
	var value string
	cb.mutex.Lock()
	defer cb.mutex.Unlock()
	cb.sema.Add(1)
	res := C.envoyGoFilterGetFilterState(cb.wrapper, unsafe.Pointer(&key), unsafe.Pointer(&value))
	if res == C.CAPIYield {
		atomic.AddInt32(&cb.waitingOnEnvoy, 1)
		cb.sema.Wait()
	} else {
		cb.sema.Done()
		// TODO: handle res
	}

	return strings.Clone(value)
}

func (c *cgoApiImpl) SetFilterState(f unsafe.Pointer, key string, value string, stateType api.StateType, lifeSpan api.LifeSpan, streamSharing api.StreamSharing) {
	cb := (*connectionCallback)(f)
	_ = C.envoyGoFilterSetFilterState(cb.wrapper, unsafe.Pointer(&key), unsafe.Pointer(&value), C.int(stateType), C.int(lifeSpan), C.int(streamSharing))
	// TODO: handle res
}

func (c *cgoApiImpl) UpstreamConnect(libraryID string, addr string, connID uint64) unsafe.Pointer {
	return unsafe.Pointer(C.envoyGoFilterUpstreamConnect(unsafe.Pointer(&libraryID), unsafe.Pointer(&addr), C.uint64_t(connID)))
}

func (c *cgoApiImpl) UpstreamConnEnableHalfClose(f unsafe.Pointer, enableHalfClose int) {
	C.envoyGoFilterUpstreamConnEnableHalfClose(f, C.int(enableHalfClose))
}

func (c *cgoApiImpl) UpstreamWrite(f unsafe.Pointer, bufferPtr unsafe.Pointer, bufferLen int, endStream int) {
	C.envoyGoFilterUpstreamWrite(f, bufferPtr, C.int(bufferLen), C.int(endStream))
}

func (c *cgoApiImpl) UpstreamClose(f unsafe.Pointer, closeType int) {
	C.envoyGoFilterUpstreamClose(f, C.int(closeType))
}

func (c *cgoApiImpl) UpstreamFinalize(f unsafe.Pointer, reason int) {
	C.envoyGoFilterUpstreamFinalize(f, C.int(reason))
}

func (c *cgoApiImpl) UpstreamInfo(f unsafe.Pointer, infoType int) string {
	var info string
	C.envoyGoFilterUpstreamInfo(f, C.int(infoType), unsafe.Pointer(&info))
	return strings.Clone(info)
}
