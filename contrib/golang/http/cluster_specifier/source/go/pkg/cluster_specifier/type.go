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

import (
	"github.com/envoyproxy/envoy/contrib/golang/http/cluster_specifier/source/go/pkg/api"
)

type httpHeaderMap struct {
	headerPtr   uint64
	headers     map[string]string
	headerNum   uint64
	headerBytes uint64
}

var _ api.HeaderMap = (*httpHeaderMap)(nil)

func (h *httpHeaderMap) GetRaw(key string) string {
	var value string
	cAPI.HttpGetHeader(h.headerPtr, &key, &value)
	return value
}

func (h *httpHeaderMap) Get(key string) (string, bool) {
	if h.headers == nil {
		h.headers = cAPI.HttpCopyHeaders(h.headerPtr, h.headerNum, h.headerBytes)
	}
	value, ok := h.headers[key]
	return value, ok
}

func (h *httpHeaderMap) Set(key, value string) {
	if h.headers != nil {
		h.headers[key] = value
	}
	cAPI.HttpSetHeader(h.headerPtr, &key, &value)
}

func (h *httpHeaderMap) Add(key, value string) {
	panic("unsupported yet")
}

func (h *httpHeaderMap) Del(key string) {
	if h.headers != nil {
		delete(h.headers, key)
	}
	cAPI.HttpRemoveHeader(h.headerPtr, &key)
}

// ByteSize return size of HeaderMap
func (h *httpHeaderMap) ByteSize() uint64 {
	return h.headerBytes
}
