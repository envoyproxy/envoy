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
	"errors"
	"fmt"
	"runtime"
	"sync"

	"github.com/envoyproxy/envoy/contrib/golang/common/go/api"
)

var (
	ErrDupRequestKey = errors.New("dup request key")
)

// wrap the UpstreamFilter to ensure that the runtime.finalizer can be triggered
// regardless of whether there is a circular reference in the UpstreamFilter.
type upstreamConnWrapper struct {
	api.TcpUpstreamFilter
}

var Requests = &requestMap{}

type requestMap struct {
	requests sync.Map // *C.httpRequest -> *httpRequest
}

func (f *requestMap) StoreReq(key *C.httpRequest, req *httpRequest) error {
	if _, loaded := f.requests.LoadOrStore(key, req); loaded {
		return ErrDupRequestKey
	}
	return nil
}

func (f *requestMap) GetReq(key *C.httpRequest) *httpRequest {
	if v, ok := f.requests.Load(key); ok {
		return v.(*httpRequest)
	}
	return nil
}

func (f *requestMap) DeleteReq(key *C.httpRequest) {
	f.requests.Delete(key)
}

func (f *requestMap) Clear() {
	f.requests.Range(func(key, _ interface{}) bool {
		f.requests.Delete(key)
		return true
	})
}

func requestFinalize(r *httpRequest) {
	r.Finalize(api.NormalFinalize)
}

func getOrCreateState(s *C.processState) *processState {
	r := s.req
	req := getRequest(r)
	if req == nil {
		req = createRequest(r)
	}
	if s.is_encoding == 0 {
		if req.decodingState.processState == nil {
			req.decodingState.processState = s
		}
		return &req.decodingState
	}

	if req.encodingState.processState == nil {
		req.encodingState.processState = s
	}
	return &req.encodingState
}

func createRequest(r *C.httpRequest) *httpRequest {
	req := &httpRequest{
		req: r,
	}
	req.decodingState.request = req
	req.encodingState.request = req
	req.streamInfo.request = req

	req.cond.L = &req.waitingLock
	// NP: make sure filter will be deleted.
	runtime.SetFinalizer(req, requestFinalize)

	err := Requests.StoreReq(r, req)
	if err != nil {
		panic(fmt.Sprintf("createRequest failed, err: %s", err.Error()))
	}

	configId := uint64(r.configId)

	filterFactory, config := getTcpUpstreamFactoryAndConfig(req.pluginName(), configId)
	f := filterFactory(config, req)
	req.tcpUpstreamFilter = f

	return req
}

func getRequest(r *C.httpRequest) *httpRequest {
	return Requests.GetReq(r)
}

//export envoyGoEncodeHeader
func envoyGoEncodeHeader(s *C.processState, endStream, headerNum, headerBytes, buffer, length uint64) uint64 {
	state := getOrCreateState(s)

	req := state.request
	buf := &httpBuffer{
		state:               state,
		envoyBufferInstance: buffer,
		length:              length,
	}
	if req.pInfo.paniced {
		return uint64(api.SendDataWithTunneling)
	}
	defer state.RecoverPanic()

	filter := req.tcpUpstreamFilter
	header := &requestHeaderMapImpl{
		requestOrResponseHeaderMapImpl{
			headerMapImpl{
				state:       state,
				headerNum:   headerNum,
				headerBytes: headerBytes,
			},
		},
	}
	return uint64(filter.EncodeHeaders(header, buf, endStream == uint64(api.EndStream)))
}

//export envoyGoEncodeData
func envoyGoEncodeData(s *C.processState, endStream, buffer, length uint64) uint64 {
	state := getOrCreateState(s)

	req := state.request
	buf := &httpBuffer{
		state:               state,
		envoyBufferInstance: buffer,
		length:              length,
	}
	if req.pInfo.paniced {
		buf.SetString(req.pInfo.details)
		return uint64(api.SendDataWithTunneling)
	}
	defer state.RecoverPanic()

	filter := req.tcpUpstreamFilter

	return uint64(filter.EncodeData(buf, endStream == uint64(api.EndStream)))
}

//export envoyGoOnUpstreamData
func envoyGoOnUpstreamData(s *C.processState, endStream, headerNum, headerBytes, buffer, length uint64) uint64 {

	state := getOrCreateState(s)

	req := state.request
	buf := &httpBuffer{
		state:               state,
		envoyBufferInstance: buffer,
		length:              length,
	}
	if req.pInfo.paniced {
		buf.SetString(req.pInfo.details)
		return uint64(api.ReceiveDataFailure)
	}
	defer state.RecoverPanic()

	filter := req.tcpUpstreamFilter
	header := &responseHeaderMapImpl{
		requestOrResponseHeaderMapImpl{
			headerMapImpl{
				state:       state,
				headerNum:   headerNum,
				headerBytes: headerBytes,
			},
		},
	}

	return uint64(filter.OnUpstreamData(header, buf, endStream == uint64(api.EndStream)))
}

//export envoyGoOnTcpUpstreamDestroy
func envoyGoOnTcpUpstreamDestroy(r *C.httpRequest, reason uint64) {
	req := getRequest(r)
	// do nothing even when req.panic is true, since filter is already destroying.
	defer req.recoverPanic()

	v := api.DestroyReason(reason)

	f := req.tcpUpstreamFilter
	f.OnDestroy(v)

	// Break circular references between httpRequest and StreamFilter,
	// since Finalizers don't work with circular references,
	// otherwise, it will leads to memory leaking.
	req.tcpUpstreamFilter = nil

	Requests.DeleteReq(r)
}
