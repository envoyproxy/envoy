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

import (
	"strconv"
	"strings"
	"sync"
	"unsafe"

	"github.com/envoyproxy/envoy/contrib/golang/common/go/api"
)

// panic error messages when C API return not ok
const (
	errRequestFinished = "request has been finished"
	errFilterDestroyed = "golang filter has been destroyed"
	errNotInGo         = "not proccessing Go"
	errInvalidPhase    = "invalid phase, maybe headers/buffer already continued"
)

// api.HeaderMap
type headerMapImpl struct {
	state       *processState
	headers     map[string][]string
	headerNum   uint64
	headerBytes uint64
	mutex       sync.Mutex
}

type requestOrResponseHeaderMapImpl struct {
	headerMapImpl
}

func (h *requestOrResponseHeaderMapImpl) initHeaders() {
	if h.headers == nil {
		h.headers = cAPI.HttpCopyHeaders(unsafe.Pointer(h.state), h.headerNum, h.headerBytes)
	}
}

func (h *requestOrResponseHeaderMapImpl) GetRaw(key string) string {
	// GetRaw is case-sensitive
	return cAPI.HttpGetHeader(unsafe.Pointer(h.state), key)
}

func (h *requestOrResponseHeaderMapImpl) Get(key string) (string, bool) {
	key = strings.ToLower(key)
	h.mutex.Lock()
	defer h.mutex.Unlock()
	h.initHeaders()
	value, ok := h.headers[key]
	if !ok {
		return "", false
	}
	return value[0], ok
}

func (h *requestOrResponseHeaderMapImpl) Values(key string) []string {
	key = strings.ToLower(key)
	h.mutex.Lock()
	defer h.mutex.Unlock()
	h.initHeaders()
	value, ok := h.headers[key]
	if !ok {
		return nil
	}
	return value
}

func (h *requestOrResponseHeaderMapImpl) Set(key, value string) {
	key = strings.ToLower(key)
	// Get all header values first before setting a value, since the set operation may not take affects immediately
	// when it's invoked in a Go thread, instead, it will post a callback to run in the envoy worker thread.
	// Otherwise, we may get outdated values in a following Get call.
	h.mutex.Lock()
	defer h.mutex.Unlock()
	h.initHeaders()
	if h.headers != nil {
		h.headers[key] = []string{value}
	}
	cAPI.HttpSetHeader(unsafe.Pointer(h.state), key, value, false)
}

func (h *requestOrResponseHeaderMapImpl) Add(key, value string) {
	key = strings.ToLower(key)
	h.mutex.Lock()
	defer h.mutex.Unlock()
	h.initHeaders()
	if h.headers != nil {
		if hdrs, found := h.headers[key]; found {
			h.headers[key] = append(hdrs, value)
		} else {
			h.headers[key] = []string{value}
		}
	}
	cAPI.HttpSetHeader(unsafe.Pointer(h.state), key, value, true)
}

func (h *requestOrResponseHeaderMapImpl) Del(key string) {
	key = strings.ToLower(key)
	// Get all header values first before removing a key, since the del operation may not take affects immediately
	// when it's invoked in a Go thread, instead, it will post a callback to run in the envoy worker thread.
	// Otherwise, we may get outdated values in a following Get call.
	h.mutex.Lock()
	defer h.mutex.Unlock()
	h.initHeaders()
	delete(h.headers, key)
	cAPI.HttpRemoveHeader(unsafe.Pointer(h.state), key)
}

func (h *requestOrResponseHeaderMapImpl) Range(f func(key, value string) bool) {
	// To avoid dead lock, methods with lock(Get, Values, Set, Add, Del) should not be used in func f.
	h.mutex.Lock()
	defer h.mutex.Unlock()
	h.initHeaders()
	for key, values := range h.headers {
		for _, value := range values {
			if !f(key, value) {
				return
			}
		}
	}
}

func (h *requestOrResponseHeaderMapImpl) RangeWithCopy(f func(key, value string) bool) {
	// There is no dead lock risk in RangeWithCopy, but copy may introduce performance cost.
	h.mutex.Lock()
	h.initHeaders()
	copied_headers := make(map[string][]string)
	for key, values := range h.headers {
		copied_headers[key] = values
	}
	h.mutex.Unlock()
	for key, values := range copied_headers {
		for _, value := range values {
			if !f(key, value) {
				return
			}
		}
	}
}

func (h *requestOrResponseHeaderMapImpl) GetAllHeaders() map[string][]string {
	h.mutex.Lock()
	defer h.mutex.Unlock()
	h.initHeaders()
	copiedHeaders := make(map[string][]string)
	for key, value := range h.headers {
		copiedHeaders[key] = make([]string, len(value))
		copy(copiedHeaders[key], value)
	}
	return copiedHeaders
}

// api.RequestHeaderMap
type requestHeaderMapImpl struct {
	requestOrResponseHeaderMapImpl
}

var _ api.RequestHeaderMap = (*requestHeaderMapImpl)(nil)

func (h *requestHeaderMapImpl) Scheme() string {
	v, _ := h.Get(":scheme")
	return v
}

func (h *requestHeaderMapImpl) Method() string {
	v, _ := h.Get(":method")
	return v
}

func (h *requestHeaderMapImpl) Path() string {
	v, _ := h.Get(":path")
	return v
}

func (h *requestHeaderMapImpl) Host() string {
	v, _ := h.Get(":authority")
	return v
}

func (h *requestHeaderMapImpl) SetMethod(method string) {
	h.Set(":method", method)
}

func (h *requestHeaderMapImpl) SetPath(path string) {
	h.Set(":path", path)
}

func (h *requestHeaderMapImpl) SetHost(host string) {
	h.Set(":authority", host)
}

// api.ResponseHeaderMap
type responseHeaderMapImpl struct {
	requestOrResponseHeaderMapImpl
}

var _ api.ResponseHeaderMap = (*responseHeaderMapImpl)(nil)

func (h *responseHeaderMapImpl) Status() (int, bool) {
	if str, ok := h.Get(":status"); ok {
		v, _ := strconv.Atoi(str)
		return v, true
	}
	return 0, false
}

type requestOrResponseTrailerMapImpl struct {
	headerMapImpl
}

func (h *requestOrResponseTrailerMapImpl) initTrailers() {
	if h.headers == nil {
		h.headers = cAPI.HttpCopyTrailers(unsafe.Pointer(h.state), h.headerNum, h.headerBytes)
	}
}

func (h *requestOrResponseTrailerMapImpl) GetRaw(key string) string {
	return cAPI.HttpGetHeader(unsafe.Pointer(h.state), key)
}

func (h *requestOrResponseTrailerMapImpl) Get(key string) (string, bool) {
	key = strings.ToLower(key)
	h.mutex.Lock()
	defer h.mutex.Unlock()
	h.initTrailers()
	value, ok := h.headers[key]
	if !ok {
		return "", false
	}
	return value[0], ok
}

func (h *requestOrResponseTrailerMapImpl) Values(key string) []string {
	key = strings.ToLower(key)
	h.mutex.Lock()
	defer h.mutex.Unlock()
	h.initTrailers()
	value, ok := h.headers[key]
	if !ok {
		return nil
	}
	return value
}

func (h *requestOrResponseTrailerMapImpl) Set(key, value string) {
	key = strings.ToLower(key)
	// Get all header values first before setting a value, since the set operation may not take affects immediately
	// when it's invoked in a Go thread, instead, it will post a callback to run in the envoy worker thread.
	// Otherwise, we may get outdated values in a following Get call.
	h.mutex.Lock()
	defer h.mutex.Unlock()
	h.initTrailers()
	if h.headers != nil {
		h.headers[key] = []string{value}
	}

	cAPI.HttpSetTrailer(unsafe.Pointer(h.state), key, value, false)
}

func (h *requestOrResponseTrailerMapImpl) Add(key, value string) {
	key = strings.ToLower(key)
	h.mutex.Lock()
	defer h.mutex.Unlock()
	h.initTrailers()
	if h.headers != nil {
		if trailers, found := h.headers[key]; found {
			h.headers[key] = append(trailers, value)
		} else {
			h.headers[key] = []string{value}
		}
	}
	cAPI.HttpSetTrailer(unsafe.Pointer(h.state), key, value, true)
}

func (h *requestOrResponseTrailerMapImpl) Del(key string) {
	key = strings.ToLower(key)
	h.mutex.Lock()
	defer h.mutex.Unlock()
	h.initTrailers()
	delete(h.headers, key)
	cAPI.HttpRemoveTrailer(unsafe.Pointer(h.state), key)
}

func (h *requestOrResponseTrailerMapImpl) Range(f func(key, value string) bool) {
	// To avoid dead lock, methods with lock(Get, Values, Set, Add, Del) should not be used in func f.
	h.mutex.Lock()
	defer h.mutex.Unlock()
	h.initTrailers()
	for key, values := range h.headers {
		for _, value := range values {
			if !f(key, value) {
				return
			}
		}
	}
}

func (h *requestOrResponseTrailerMapImpl) RangeWithCopy(f func(key, value string) bool) {
	// There is no dead lock risk in RangeWithCopy, but copy may introduce performance cost.
	h.mutex.Lock()
	h.initTrailers()
	copied_headers := make(map[string][]string)
	for key, values := range h.headers {
		copied_headers[key] = values
	}
	h.mutex.Unlock()
	for key, values := range copied_headers {
		for _, value := range values {
			if !f(key, value) {
				return
			}
		}
	}
}

func (h *requestOrResponseTrailerMapImpl) GetAllHeaders() map[string][]string {
	h.mutex.Lock()
	defer h.mutex.Unlock()
	h.initTrailers()
	copiedHeaders := make(map[string][]string)
	for key, value := range h.headers {
		copiedHeaders[key] = make([]string, len(value))
		copy(copiedHeaders[key], value)
	}
	return copiedHeaders
}

// api.RequestTrailerMap
type requestTrailerMapImpl struct {
	requestOrResponseTrailerMapImpl
}

var _ api.RequestTrailerMap = (*requestTrailerMapImpl)(nil)

// api.ResponseTrailerMap
type responseTrailerMapImpl struct {
	requestOrResponseTrailerMapImpl
}

var _ api.ResponseTrailerMap = (*responseTrailerMapImpl)(nil)

// api.BufferInstance
type httpBuffer struct {
	state               *processState
	envoyBufferInstance uint64
	length              uint64
	value               []byte
}

var _ api.BufferInstance = (*httpBuffer)(nil)

func (b *httpBuffer) Write(p []byte) (n int, err error) {
	cAPI.HttpSetBytesBufferHelper(unsafe.Pointer(b.state), b.envoyBufferInstance, p, api.AppendBuffer)
	n = len(p)
	b.length += uint64(n)
	return n, nil
}

func (b *httpBuffer) WriteString(s string) (n int, err error) {
	cAPI.HttpSetBufferHelper(unsafe.Pointer(b.state), b.envoyBufferInstance, s, api.AppendBuffer)
	n = len(s)
	b.length += uint64(n)
	return n, nil
}

func (b *httpBuffer) WriteByte(p byte) error {
	cAPI.HttpSetBufferHelper(unsafe.Pointer(b.state), b.envoyBufferInstance, string(p), api.AppendBuffer)
	b.length++
	return nil
}

func (b *httpBuffer) WriteUint16(p uint16) error {
	s := strconv.FormatUint(uint64(p), 10)
	_, err := b.WriteString(s)
	return err
}

func (b *httpBuffer) WriteUint32(p uint32) error {
	s := strconv.FormatUint(uint64(p), 10)
	_, err := b.WriteString(s)
	return err
}

func (b *httpBuffer) WriteUint64(p uint64) error {
	s := strconv.FormatUint(p, 10)
	_, err := b.WriteString(s)
	return err
}

func (b *httpBuffer) Bytes() []byte {
	if b.length == 0 {
		return nil
	}
	b.value = cAPI.HttpGetBuffer(unsafe.Pointer(b.state), b.envoyBufferInstance, b.length)
	return b.value
}

func (b *httpBuffer) Drain(offset int) {
	if offset <= 0 || b.length == 0 {
		return
	}

	size := uint64(offset)
	if size > b.length {
		size = b.length
	}

	cAPI.HttpDrainBuffer(unsafe.Pointer(b.state), b.envoyBufferInstance, size)

	b.length -= size
}

func (b *httpBuffer) Len() int {
	return int(b.length)
}

func (b *httpBuffer) Reset() {
	b.Drain(b.Len())
}

func (b *httpBuffer) String() string {
	if b.length == 0 {
		return ""
	}
	b.value = cAPI.HttpGetBuffer(unsafe.Pointer(b.state), b.envoyBufferInstance, b.length)
	return string(b.value)
}

func (b *httpBuffer) Append(data []byte) error {
	_, err := b.Write(data)
	return err
}

func (b *httpBuffer) Prepend(data []byte) error {
	cAPI.HttpSetBytesBufferHelper(unsafe.Pointer(b.state), b.envoyBufferInstance, data, api.PrependBuffer)
	b.length += uint64(len(data))
	return nil
}

func (b *httpBuffer) AppendString(s string) error {
	_, err := b.WriteString(s)
	return err
}

func (b *httpBuffer) PrependString(s string) error {
	cAPI.HttpSetBufferHelper(unsafe.Pointer(b.state), b.envoyBufferInstance, s, api.PrependBuffer)
	b.length += uint64(len(s))
	return nil
}

func (b *httpBuffer) Set(data []byte) error {
	cAPI.HttpSetBytesBufferHelper(unsafe.Pointer(b.state), b.envoyBufferInstance, data, api.SetBuffer)
	b.length = uint64(len(data))
	return nil
}

func (b *httpBuffer) SetString(s string) error {
	cAPI.HttpSetBufferHelper(unsafe.Pointer(b.state), b.envoyBufferInstance, s, api.SetBuffer)
	b.length = uint64(len(s))
	return nil
}
