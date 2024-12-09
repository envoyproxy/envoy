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

#cgo CFLAGS: -I../../../../../../../../common/go/api -I../api
#cgo linux LDFLAGS: -Wl,-unresolved-symbols=ignore-all
#cgo darwin LDFLAGS: -Wl,-undefined,dynamic_lookup

#include <stdlib.h>
#include <string.h>

#include "api.h"

*/
import "C"
import (
	"runtime"
	"strings"
	"unsafe"

	"github.com/envoyproxy/envoy/contrib/golang/common/go/api"
	_ "github.com/envoyproxy/envoy/contrib/golang/common/go/api_impl"
)

const (
	ValueRouteName   = 1
	ValueClusterName = 2

	// NOTE: this is a trade-off value.
	// When the number of header is less this value, we could use the slice on the stack,
	// otherwise, we have to allocate a new slice on the heap,
	// and the slice on the stack will be wasted.
	// So, we choose a value that many requests' number of header is less than this value.
	// But also, it should not be too large, otherwise it might be waste stack memory.
	maxStackAllocedHeaderSize = 16
	maxStackAllocedSliceLen   = maxStackAllocedHeaderSize * 2
)

var cAPI api.TcpUpstreamCAPI = &cgoApiImpl{}

func SetCgoAPI(apiImpl api.TcpUpstreamCAPI) {
	if apiImpl != nil {
		cAPI = apiImpl
	}
}

type cgoApiImpl struct{}

// When the status means unexpected stage when invoke C API,
// panic here and it will be recover in the Go entry function.
func handleCApiStatus(status C.CAPIStatus) {
	switch status {
	case C.CAPIInvalidPhase:
		panic(capiStatusToStr(status))
	}
}

func capiStatusToStr(status C.CAPIStatus) string {
	switch status {
	case C.CAPIInvalidPhase:
		return errInvalidPhase
	}

	return "unknown status"
}

func (c *cgoApiImpl) CopyHeaders(r unsafe.Pointer, num uint64, bytes uint64) map[string][]string {
	req := (*httpRequest)(r)
	var strs []string
	if num <= maxStackAllocedHeaderSize {
		// NOTE: only const length slice may be allocated on stack.
		strs = make([]string, maxStackAllocedSliceLen)
	} else {
		// TODO: maybe we could use a memory pool for better performance,
		// since these go strings in strs, will be copied into the following map.
		strs = make([]string, num*2)
	}
	// NOTE: this buffer can not be reused safely,
	// since strings may refer to this buffer as string data, and string is const in go.
	// we have to make sure the all strings is not using before reusing,
	// but strings may be alive beyond the request life.
	buf := make([]byte, bytes)
	res := C.envoyGoTcpUpstreamCopyHeaders(unsafe.Pointer(req.req), unsafe.Pointer(unsafe.SliceData(strs)), unsafe.Pointer(unsafe.SliceData(buf)))
	handleCApiStatus(res)

	m := make(map[string][]string, num)
	for i := uint64(0); i < num*2; i += 2 {
		key := strs[i]
		value := strs[i+1]

		if v, found := m[key]; !found {
			m[key] = []string{value}
		} else {
			m[key] = append(v, value)
		}
	}
	runtime.KeepAlive(buf)
	return m
}

func (c *cgoApiImpl) SetRespHeader(r unsafe.Pointer, key string, value string, add bool) {
	req := (*httpRequest)(r)
	var act C.headerAction
	if add {
		act = C.HeaderAdd
	} else {
		act = C.HeaderSet
	}
	res := C.envoyGoTcpUpstreamSetRespHeader(unsafe.Pointer(req.req), unsafe.Pointer(unsafe.StringData(key)), C.int(len(key)),
		unsafe.Pointer(unsafe.StringData(value)), C.int(len(value)), act)
	handleCApiStatus(res)
}

func (c *cgoApiImpl) GetBuffer(r unsafe.Pointer, bufferPtr uint64, length uint64) []byte {
	req := (*httpRequest)(r)
	buf := make([]byte, length)
	res := C.envoyGoTcpUpstreamGetBuffer(unsafe.Pointer(req.req), C.uint64_t(bufferPtr), unsafe.Pointer(unsafe.SliceData(buf)))
	handleCApiStatus(res)
	return unsafe.Slice(unsafe.SliceData(buf), length)
}

func (c *cgoApiImpl) DrainBuffer(r unsafe.Pointer, bufferPtr uint64, length uint64) {
	req := (*httpRequest)(r)
	res := C.envoyGoTcpUpstreamDrainBuffer(unsafe.Pointer(req.req), C.uint64_t(bufferPtr), C.uint64_t(length))
	handleCApiStatus(res)
}

func (c *cgoApiImpl) SetBufferHelper(r unsafe.Pointer, bufferPtr uint64, value string, action api.BufferAction) {
	req := (*httpRequest)(r)
	c.setBufferHelper(req, bufferPtr, unsafe.Pointer(unsafe.StringData(value)), C.int(len(value)), action)
}

func (c *cgoApiImpl) SetBytesBufferHelper(r unsafe.Pointer, bufferPtr uint64, value []byte, action api.BufferAction) {
	req := (*httpRequest)(r)
	c.setBufferHelper(req, bufferPtr, unsafe.Pointer(unsafe.SliceData(value)), C.int(len(value)), action)
}

func (c *cgoApiImpl) setBufferHelper(req *httpRequest, bufferPtr uint64, data unsafe.Pointer, length C.int, action api.BufferAction) {
	var act C.bufferAction
	switch action {
	case api.SetBuffer:
		act = C.Set
	case api.AppendBuffer:
		act = C.Append
	case api.PrependBuffer:
		act = C.Prepend
	}
	res := C.envoyGoTcpUpstreamSetBufferHelper(unsafe.Pointer(req.req), C.uint64_t(bufferPtr), data, length, act)
	handleCApiStatus(res)
}

func (c *cgoApiImpl) GetStringValue(r unsafe.Pointer, id int) (string, bool) {
	req := (*httpRequest)(r)
	// add a lock to protect filter->req_->strValue field in the Envoy side, from being writing concurrency,
	// since there might be multiple concurrency goroutines invoking this API on the Go side.
	req.mutex.Lock()
	defer req.mutex.Unlock()

	var valueData C.uint64_t
	var valueLen C.int
	res := C.envoyGoTcpUpstreamGetStringValue(unsafe.Pointer(req.req), C.int(id), &valueData, &valueLen)
	handleCApiStatus(res)
	value := unsafe.String((*byte)(unsafe.Pointer(uintptr(valueData))), int(valueLen))
	// copy the memory from c to Go.
	return strings.Clone(value), true
}

func (c *cgoApiImpl) SetSelfHalfCloseForUpstreamConn(r unsafe.Pointer, enabled int) {
	req := (*httpRequest)(r)
	// add a lock to protect filter->req_->strValue field in the Envoy side, from being writing concurrency,
	// since there might be multiple concurrency goroutines invoking this API on the Go side.
	req.mutex.Lock()
	defer req.mutex.Unlock()

	res := C.envoyGoTcpUpstreamSetSelfHalfCloseForUpstreamConn(unsafe.Pointer(req.req), C.int(enabled))
	handleCApiStatus(res)
}
