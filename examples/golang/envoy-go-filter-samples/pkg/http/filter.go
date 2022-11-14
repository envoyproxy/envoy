/*
 * Licensed to the Apache Software Foundation (ASF) under one or more
 * contributor license agreements. See the NOTICE file distributed with
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

#cgo linux LDFLAGS: -Wl,-unresolved-symbols=ignore-all
#cgo darwin LDFLAGS: -Wl,-undefined,dynamic_lookup

#include <stdlib.h>
#include <string.h>

#include "api.h"

*/
import "C"
import (
        "fmt"
        "unsafe"

        "github.com/envoyproxy/envoy/examples/golang/envoy-go-filter-samples/pkg/http/api"
)

type httpRequest struct {
        req        *C.httpRequest
        httpFilter api.HttpFilter
}

func (r *httpRequest) Continue(status api.StatusType) {
        if status == api.LocalReply {
                fmt.Printf("warning: LocalReply status is useless after sendLocalReply, ignoring")
                return
        }
        cAPI.HttpContinue(unsafe.Pointer(r.req), uint64(status))
}

func (r *httpRequest) SendLocalReply(response_code int, body_text string, headers map[string]string, grpc_status int64, details string) {
        cAPI.HttpSendLocalReply(unsafe.Pointer(r.req), response_code, body_text, headers, grpc_status, details)
}

func (r *httpRequest) Finalize(reason int) {
        cAPI.HttpFinalize(unsafe.Pointer(r.req), reason)
}

type httpHeaderMap struct {
        request     *httpRequest
        headers     map[string]string
        headerNum   uint64
        headerBytes uint64
        isTrailer   bool
}

func (h *httpHeaderMap) GetRaw(name string) string {
        if h.isTrailer {
                panic("unsupported yet")
        }
        var value string
        cAPI.HttpGetHeader(unsafe.Pointer(h.request.req), &name, &value)
        return value
}

func (h *httpHeaderMap) Get(name string) string {
        if h.headers == nil {
                if h.isTrailer {
                        h.headers = cAPI.HttpCopyTrailers(unsafe.Pointer(h.request.req), h.headerNum, h.headerBytes)
                } else {
                        h.headers = cAPI.HttpCopyHeaders(unsafe.Pointer(h.request.req), h.headerNum, h.headerBytes)
                }
        }
        if value, ok := h.headers[name]; ok {
                return value
        }
        return ""
}

func (h *httpHeaderMap) Set(name, value string) {
        if h.headers != nil {
                h.headers[name] = value
        }
        if h.isTrailer {
                cAPI.HttpSetTrailer(unsafe.Pointer(h.request.req), &name, &value)
        } else {
                cAPI.HttpSetHeader(unsafe.Pointer(h.request.req), &name, &value)
        }
}

func (h *httpHeaderMap) Remove(name string) {
        if h.headers != nil {
                delete(h.headers, name)
        }
        if h.isTrailer {
                panic("unsupported yet")
        } else {
                cAPI.HttpRemoveHeader(unsafe.Pointer(h.request.req), &name)
        }
}

type httpBuffer struct {
        request   *httpRequest
        bufferPtr uint64
        length    uint64
        value     string
}

func (b *httpBuffer) GetString() string {
        if b.length == 0 {
                return ""
        }
        cAPI.HttpGetBuffer(unsafe.Pointer(b.request.req), b.bufferPtr, &b.value, b.length)
        return b.value
}

func (b *httpBuffer) Set(value string) {
        cAPI.HttpSetBuffer(unsafe.Pointer(b.request.req), b.bufferPtr, value)
}

func (b *httpBuffer) Length() uint64 {
        return b.length
}
