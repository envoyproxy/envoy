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

package http

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
	"errors"
	"runtime"
	"strings"
	"unsafe"

	"google.golang.org/protobuf/proto"
	"google.golang.org/protobuf/types/known/structpb"

	"github.com/envoyproxy/envoy/contrib/golang/common/go/api"
	_ "github.com/envoyproxy/envoy/contrib/golang/common/go/api_impl"
)

const (
	ValueRouteName               = 1
	ValueFilterChainName         = 2
	ValueProtocol                = 3
	ValueResponseCode            = 4
	ValueResponseCodeDetails     = 5
	ValueAttemptCount            = 6
	ValueDownstreamLocalAddress  = 7
	ValueDownstreamRemoteAddress = 8
	ValueUpstreamLocalAddress    = 9
	ValueUpstreamRemoteAddress   = 10
	ValueUpstreamClusterName     = 11
	ValueVirtualClusterName      = 12

	// NOTE: this is a trade-off value.
	// When the number of header is less this value, we could use the slice on the stack,
	// otherwise, we have to allocate a new slice on the heap,
	// and the slice on the stack will be wasted.
	// So, we choose a value that many requests' number of header is less than this value.
	// But also, it should not be too large, otherwise it might be waste stack memory.
	maxStackAllocedHeaderSize = 16
	maxStackAllocedSliceLen   = maxStackAllocedHeaderSize * 2
)

type httpCApiImpl struct{}

// When the status means unexpected stage when invoke C API,
// panic here and it will be recover in the Go entry function.
func handleCApiStatus(status C.CAPIStatus) {
	switch status {
	case C.CAPIFilterIsGone,
		C.CAPIFilterIsDestroy,
		C.CAPINotInGo,
		C.CAPIInvalidPhase:
		panic(capiStatusToStr(status))
	}
}

func capiStatusToStr(status C.CAPIStatus) string {
	switch status {
	case C.CAPIFilterIsGone:
		return errRequestFinished
	case C.CAPIFilterIsDestroy:
		return errFilterDestroyed
	case C.CAPINotInGo:
		return errNotInGo
	case C.CAPIInvalidPhase:
		return errInvalidPhase
	}

	return "unknown status"
}

func capiStatusToErr(status C.CAPIStatus) error {
	switch status {
	case C.CAPIValueNotFound:
		return api.ErrValueNotFound
	case C.CAPIInternalFailure:
		return api.ErrInternalFailure
	case C.CAPISerializationFailure:
		return api.ErrSerializationFailure
	}

	return errors.New("unknown status")
}

func (c *httpCApiImpl) HttpContinue(s unsafe.Pointer, status uint64) {
	state := (*processState)(s)
	res := C.envoyGoFilterHttpContinue(unsafe.Pointer(state.processState), C.int(status))
	handleCApiStatus(res)
}

// Only may panic with errRequestFinished, errFilterDestroyed or errNotInGo,
// won't panic with errInvalidPhase and others, otherwise will cause deadloop, see RecoverPanic for the details.
func (c *httpCApiImpl) HttpSendLocalReply(s unsafe.Pointer, responseCode int, bodyText string, headers map[string][]string, grpcStatus int64, details string) {
	state := (*processState)(s)
	hLen := len(headers)
	strs := make([]*C.char, 0, hLen*2)
	defer func() {
		for _, s := range strs {
			C.free(unsafe.Pointer(s))
		}
	}()
	// TODO: use runtime.Pinner after go1.22 release for better performance.
	for k, h := range headers {
		for _, v := range h {
			keyStr := C.CString(k)
			valueStr := C.CString(v)
			strs = append(strs, keyStr, valueStr)
		}
	}
	res := C.envoyGoFilterHttpSendLocalReply(unsafe.Pointer(state.processState), C.int(responseCode),
		unsafe.Pointer(unsafe.StringData(bodyText)), C.int(len(bodyText)),
		unsafe.Pointer(unsafe.SliceData(strs)), C.int(len(strs)),
		C.longlong(grpcStatus), unsafe.Pointer(unsafe.StringData(details)), C.int(len(details)))
	handleCApiStatus(res)
}

func (c *httpCApiImpl) HttpSendPanicReply(s unsafe.Pointer, details string) {
	state := (*processState)(s)
	res := C.envoyGoFilterHttpSendPanicReply(unsafe.Pointer(state.processState), unsafe.Pointer(unsafe.StringData(details)), C.int(len(details)))
	handleCApiStatus(res)
}

func (c *httpCApiImpl) HttpGetHeader(s unsafe.Pointer, key string) string {
	state := (*processState)(s)
	var valueData C.uint64_t
	var valueLen C.int
	res := C.envoyGoFilterHttpGetHeader(unsafe.Pointer(state.processState), unsafe.Pointer(unsafe.StringData(key)), C.int(len(key)), &valueData, &valueLen)
	handleCApiStatus(res)
	return unsafe.String((*byte)(unsafe.Pointer(uintptr(valueData))), int(valueLen))
}

func (c *httpCApiImpl) HttpCopyHeaders(s unsafe.Pointer, num uint64, bytes uint64) map[string][]string {
	state := (*processState)(s)
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
	res := C.envoyGoFilterHttpCopyHeaders(unsafe.Pointer(state.processState), unsafe.Pointer(unsafe.SliceData(strs)), unsafe.Pointer(unsafe.SliceData(buf)))
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

func (c *httpCApiImpl) HttpSetHeader(s unsafe.Pointer, key string, value string, add bool) {
	state := (*processState)(s)
	var act C.headerAction
	if add {
		act = C.HeaderAdd
	} else {
		act = C.HeaderSet
	}
	res := C.envoyGoFilterHttpSetHeaderHelper(unsafe.Pointer(state.processState), unsafe.Pointer(unsafe.StringData(key)), C.int(len(key)),
		unsafe.Pointer(unsafe.StringData(value)), C.int(len(value)), act)
	handleCApiStatus(res)
}

func (c *httpCApiImpl) HttpRemoveHeader(s unsafe.Pointer, key string) {
	state := (*processState)(s)
	res := C.envoyGoFilterHttpRemoveHeader(unsafe.Pointer(state.processState), unsafe.Pointer(unsafe.StringData(key)), C.int(len(key)))
	handleCApiStatus(res)
}

func (c *httpCApiImpl) HttpGetBuffer(s unsafe.Pointer, bufferPtr uint64, length uint64) []byte {
	state := (*processState)(s)
	buf := make([]byte, length)
	res := C.envoyGoFilterHttpGetBuffer(unsafe.Pointer(state.processState), C.uint64_t(bufferPtr), unsafe.Pointer(unsafe.SliceData(buf)))
	handleCApiStatus(res)
	return unsafe.Slice(unsafe.SliceData(buf), length)
}

func (c *httpCApiImpl) HttpDrainBuffer(s unsafe.Pointer, bufferPtr uint64, length uint64) {
	state := (*processState)(s)
	res := C.envoyGoFilterHttpDrainBuffer(unsafe.Pointer(state.processState), C.uint64_t(bufferPtr), C.uint64_t(length))
	handleCApiStatus(res)
}

func (c *httpCApiImpl) HttpSetBufferHelper(s unsafe.Pointer, bufferPtr uint64, value string, action api.BufferAction) {
	state := (*processState)(s)
	c.httpSetBufferHelper(state, bufferPtr, unsafe.Pointer(unsafe.StringData(value)), C.int(len(value)), action)
}

func (c *httpCApiImpl) HttpSetBytesBufferHelper(s unsafe.Pointer, bufferPtr uint64, value []byte, action api.BufferAction) {
	state := (*processState)(s)
	c.httpSetBufferHelper(state, bufferPtr, unsafe.Pointer(unsafe.SliceData(value)), C.int(len(value)), action)
}

func (c *httpCApiImpl) httpSetBufferHelper(state *processState, bufferPtr uint64, data unsafe.Pointer, length C.int, action api.BufferAction) {
	var act C.bufferAction
	switch action {
	case api.SetBuffer:
		act = C.Set
	case api.AppendBuffer:
		act = C.Append
	case api.PrependBuffer:
		act = C.Prepend
	}
	res := C.envoyGoFilterHttpSetBufferHelper(unsafe.Pointer(state.processState), C.uint64_t(bufferPtr), data, length, act)
	handleCApiStatus(res)
}

func (c *httpCApiImpl) HttpCopyTrailers(s unsafe.Pointer, num uint64, bytes uint64) map[string][]string {
	state := (*processState)(s)
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
	res := C.envoyGoFilterHttpCopyTrailers(unsafe.Pointer(state.processState), unsafe.Pointer(unsafe.SliceData(strs)), unsafe.Pointer(unsafe.SliceData(buf)))
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

func (c *httpCApiImpl) HttpSetTrailer(s unsafe.Pointer, key string, value string, add bool) {
	state := (*processState)(s)
	var act C.headerAction
	if add {
		act = C.HeaderAdd
	} else {
		act = C.HeaderSet
	}
	res := C.envoyGoFilterHttpSetTrailer(unsafe.Pointer(state.processState), unsafe.Pointer(unsafe.StringData(key)), C.int(len(key)),
		unsafe.Pointer(unsafe.StringData(value)), C.int(len(value)), act)
	handleCApiStatus(res)
}

func (c *httpCApiImpl) HttpRemoveTrailer(s unsafe.Pointer, key string) {
	state := (*processState)(s)
	res := C.envoyGoFilterHttpRemoveTrailer(unsafe.Pointer(state.processState), unsafe.Pointer(unsafe.StringData(key)), C.int(len(key)))
	handleCApiStatus(res)
}

func (c *httpCApiImpl) ClearRouteCache(r unsafe.Pointer) {
	req := (*httpRequest)(r)
	res := C.envoyGoFilterHttpClearRouteCache(unsafe.Pointer(req.req))
	handleCApiStatus(res)
}

func (c *httpCApiImpl) HttpGetStringValue(r unsafe.Pointer, id int) (string, bool) {
	req := (*httpRequest)(r)
	// add a lock to protect filter->req_->strValue field in the Envoy side, from being writing concurrency,
	// since there might be multiple concurrency goroutines invoking this API on the Go side.
	req.mutex.Lock()
	defer req.mutex.Unlock()

	var valueData C.uint64_t
	var valueLen C.int
	res := C.envoyGoFilterHttpGetStringValue(unsafe.Pointer(req.req), C.int(id), &valueData, &valueLen)
	if res == C.CAPIValueNotFound {
		return "", false
	}
	handleCApiStatus(res)
	value := unsafe.String((*byte)(unsafe.Pointer(uintptr(valueData))), int(valueLen))
	// copy the memory from c to Go.
	return strings.Clone(value), true
}

func (c *httpCApiImpl) HttpGetIntegerValue(r unsafe.Pointer, id int) (uint64, bool) {
	req := (*httpRequest)(r)
	var value C.uint64_t
	res := C.envoyGoFilterHttpGetIntegerValue(unsafe.Pointer(req.req), C.int(id), &value)
	if res == C.CAPIValueNotFound {
		return 0, false
	}
	handleCApiStatus(res)
	return uint64(value), true
}

func (c *httpCApiImpl) HttpGetDynamicMetadata(r unsafe.Pointer, filterName string) map[string]interface{} {
	req := (*httpRequest)(r)
	req.mutex.Lock()
	defer req.mutex.Unlock()
	req.markMayWaitingCallback()

	var valueData C.uint64_t
	var valueLen C.int
	res := C.envoyGoFilterHttpGetDynamicMetadata(unsafe.Pointer(req.req),
		unsafe.Pointer(unsafe.StringData(filterName)), C.int(len(filterName)), &valueData, &valueLen)
	if res == C.CAPIYield {
		req.checkOrWaitCallback()
	} else {
		req.markNoWaitingCallback()
		handleCApiStatus(res)
	}
	buf := unsafe.Slice((*byte)(unsafe.Pointer(uintptr(valueData))), int(valueLen))
	// copy the memory from c to Go.
	var meta structpb.Struct
	proto.Unmarshal(buf, &meta)
	return meta.AsMap()
}

func (c *httpCApiImpl) HttpSetDynamicMetadata(r unsafe.Pointer, filterName string, key string, value interface{}) {
	req := (*httpRequest)(r)
	v, err := structpb.NewValue(value)
	if err != nil {
		panic(err)
	}
	buf, err := proto.Marshal(v)
	if err != nil {
		panic(err)
	}
	res := C.envoyGoFilterHttpSetDynamicMetadata(unsafe.Pointer(req.req),
		unsafe.Pointer(unsafe.StringData(filterName)), C.int(len(filterName)),
		unsafe.Pointer(unsafe.StringData(key)), C.int(len(key)),
		unsafe.Pointer(unsafe.SliceData(buf)), C.int(len(buf)))
	handleCApiStatus(res)
}

func (c *httpCApiImpl) HttpFinalize(r unsafe.Pointer, reason int) {
	req := (*httpRequest)(r)
	C.envoyGoFilterHttpFinalize(unsafe.Pointer(req.req), C.int(reason))
}

func (c *httpCApiImpl) HttpSetStringFilterState(r unsafe.Pointer, key string, value string, stateType api.StateType, lifeSpan api.LifeSpan, streamSharing api.StreamSharing) {
	req := (*httpRequest)(r)
	res := C.envoyGoFilterHttpSetStringFilterState(unsafe.Pointer(req.req),
		unsafe.Pointer(unsafe.StringData(key)), C.int(len(key)),
		unsafe.Pointer(unsafe.StringData(value)), C.int(len(value)),
		C.int(stateType), C.int(lifeSpan), C.int(streamSharing))
	handleCApiStatus(res)
}

func (c *httpCApiImpl) HttpGetStringFilterState(r unsafe.Pointer, key string) string {
	req := (*httpRequest)(r)
	var valueData C.uint64_t
	var valueLen C.int
	req.mutex.Lock()
	defer req.mutex.Unlock()
	req.markMayWaitingCallback()
	res := C.envoyGoFilterHttpGetStringFilterState(unsafe.Pointer(req.req),
		unsafe.Pointer(unsafe.StringData(key)), C.int(len(key)), &valueData, &valueLen)
	if res == C.CAPIYield {
		req.checkOrWaitCallback()
	} else {
		req.markNoWaitingCallback()
		handleCApiStatus(res)
	}

	value := unsafe.String((*byte)(unsafe.Pointer(uintptr(valueData))), int(valueLen))
	return strings.Clone(value)
}

func (c *httpCApiImpl) HttpGetStringProperty(r unsafe.Pointer, key string) (string, error) {
	req := (*httpRequest)(r)
	var valueData C.uint64_t
	var valueLen C.int
	var rc C.int
	req.mutex.Lock()
	defer req.mutex.Unlock()
	req.markMayWaitingCallback()
	res := C.envoyGoFilterHttpGetStringProperty(unsafe.Pointer(req.req),
		unsafe.Pointer(unsafe.StringData(key)), C.int(len(key)), &valueData, &valueLen, &rc)
	if res == C.CAPIYield {
		req.checkOrWaitCallback()
		res = C.CAPIStatus(rc)
	} else {
		req.markNoWaitingCallback()
		handleCApiStatus(res)
	}

	if res == C.CAPIOK {
		value := unsafe.String((*byte)(unsafe.Pointer(uintptr(valueData))), int(valueLen))
		return strings.Clone(value), nil
	}

	return "", capiStatusToErr(res)
}

func (c *httpCApiImpl) HttpLog(level api.LogType, message string) {
	C.envoyGoFilterLog(C.uint32_t(level), unsafe.Pointer(unsafe.StringData(message)), C.int(len(message)))
}

func (c *httpCApiImpl) HttpLogLevel() api.LogType {
	return api.GetLogLevel()
}

var cAPI api.HttpCAPI = &httpCApiImpl{}

// SetHttpCAPI for mock cAPI
func SetHttpCAPI(api api.HttpCAPI) {
	cAPI = api
}

func (c *httpCApiImpl) HttpConfigFinalize(cfg unsafe.Pointer) {
	C.envoyGoConfigHttpFinalize(cfg)
}

func (c *httpCApiImpl) HttpDefineMetric(cfg unsafe.Pointer, metricType api.MetricType, name string) uint32 {
	var value C.uint32_t
	res := C.envoyGoFilterHttpDefineMetric(cfg, C.uint32_t(metricType), unsafe.Pointer(unsafe.StringData(name)), C.int(len(name)), &value)
	handleCApiStatus(res)
	return uint32(value)
}

func (c *httpCApiImpl) HttpIncrementMetric(cc unsafe.Pointer, metricId uint32, offset int64) {
	cfg := (*httpConfig)(cc)
	res := C.envoyGoFilterHttpIncrementMetric(unsafe.Pointer(cfg.config), C.uint32_t(metricId), C.int64_t(offset))
	handleCApiStatus(res)
}

func (c *httpCApiImpl) HttpGetMetric(cc unsafe.Pointer, metricId uint32) uint64 {
	cfg := (*httpConfig)(cc)
	var value C.uint64_t
	res := C.envoyGoFilterHttpGetMetric(unsafe.Pointer(cfg.config), C.uint32_t(metricId), &value)
	handleCApiStatus(res)
	return uint64(value)
}

func (c *httpCApiImpl) HttpRecordMetric(cc unsafe.Pointer, metricId uint32, value uint64) {
	cfg := (*httpConfig)(cc)
	res := C.envoyGoFilterHttpRecordMetric(unsafe.Pointer(cfg.config), C.uint32_t(metricId), C.uint64_t(value))
	handleCApiStatus(res)
}
