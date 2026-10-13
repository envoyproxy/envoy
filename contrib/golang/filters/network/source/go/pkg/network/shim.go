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
	"errors"
	"runtime"
	"strings"
	"sync"
	"sync/atomic"
	"unsafe"

	"google.golang.org/protobuf/proto"
	"google.golang.org/protobuf/types/known/anypb"

	"github.com/envoyproxy/envoy/contrib/golang/common/go/api"
	"github.com/envoyproxy/envoy/contrib/golang/common/go/utils"
)

var (
	// ref: https://golang.org/cmd/cgo/
	// The size of any C type T is available as C.sizeof_T, as in C.sizeof_struct_stat.
	CULLSize uintptr = C.sizeof_ulonglong

	ErrDupRequestKey = errors.New("dup request key")

	DownstreamFilters = &DownstreamFilterMap{}
	UpstreamFilters   = &UpstreamFilterMap{}

	configIDGenerator uint64
	configCache       = &sync.Map{} // uint64 -> *anypb.Any

	upstreamConnIDGenerator uint64

	libraryID string
)

// wrap the UpstreamFilter to ensure that the runtime.finalizer can be triggered
// regardless of whether there is a circular reference in the UpstreamFilter.
type upstreamConnWrapper struct {
	api.UpstreamFilter
	finalizer *int
}

func CreateUpstreamConn(addr string, filter api.UpstreamFilter) {
	conn := &upstreamConnWrapper{
		UpstreamFilter: filter,
		finalizer:      new(int),
	}
	connID := atomic.AddUint64(&upstreamConnIDGenerator, 1)
	_ = UpstreamFilters.StoreFilterByConnID(connID, conn)

	h := cgoAPI.UpstreamConnect(libraryID, addr, connID)

	// NP: make sure filter will be deleted.
	runtime.SetFinalizer(conn.finalizer, func(_ *int) {
		cgoAPI.UpstreamFinalize(unsafe.Pointer(uintptr(h)), api.NormalFinalize)
	})
}

//export envoyGoFilterOnNetworkFilterConfig
func envoyGoFilterOnNetworkFilterConfig(libraryIDPtr uint64, libraryIDLen uint64, configPtr uint64, configLen uint64) uint64 {
	buf := utils.BytesToSlice(configPtr, configLen)
	var any anypb.Any
	proto.Unmarshal(buf, &any)

	libraryID = strings.Clone(utils.BytesToString(libraryIDPtr, libraryIDLen))
	configID := atomic.AddUint64(&configIDGenerator, 1)
	configCache.Store(configID, GetNetworkFilterConfigParser().ParseConfig(&any))

	return configID
}

//export envoyGoFilterOnDownstreamConnection
func envoyGoFilterOnDownstreamConnection(wrapper unsafe.Pointer, pluginNamePtr uint64, pluginNameLen uint64,
	configID uint64) uint64 {
	filterFactoryConfig, ok := configCache.Load(configID)
	if !ok {
		// TODO: panic
		return uint64(api.NetworkFilterStopIteration)
	}
	pluginName := strings.Clone(utils.BytesToString(pluginNamePtr, pluginNameLen))
	filterFactory := GetNetworkFilterConfigFactory(pluginName).CreateFactoryFromConfig(filterFactoryConfig)

	cb := &connectionCallback{
		wrapper:   wrapper,
		writeFunc: cgoAPI.DownstreamWrite,
		closeFunc: cgoAPI.DownstreamClose,
		infoFunc:  cgoAPI.DownstreamInfo,
		state: &filterState{
			wrapper: wrapper,
			setFunc: cgoAPI.SetFilterState,
			getFunc: cgoAPI.GetFilterState,
		},
	}
	filter := filterFactory.CreateFilter(cb)

	// NP: make sure filter will be deleted.
	runtime.SetFinalizer(filter, func(ff api.DownstreamFilter) {
		cgoAPI.DownstreamFinalize(unsafe.Pointer(uintptr(wrapper)), api.NormalFinalize)
	})

	// TODO: handle error
	_ = DownstreamFilters.StoreFilter(uint64(uintptr(wrapper)), &downstreamFilterWrapper{
		filter: filter,
		cb:     cb,
	})

	return uint64(filter.OnNewConnection())
}

//export envoyGoFilterOnDownstreamData
func envoyGoFilterOnDownstreamData(wrapper unsafe.Pointer, dataSize uint64, dataPtr uint64, sliceNum int, endOfStream int) uint64 {
	filter := DownstreamFilters.GetFilter(uint64(uintptr(wrapper)))

	var buf []byte

	for i := 0; i < sliceNum; i++ {
		slicePtr := dataPtr + uint64(i)*uint64(CULLSize+CULLSize)
		sliceData := *((*uint64)(unsafe.Pointer(uintptr(slicePtr))))
		sliceLen := *((*uint64)(unsafe.Pointer(uintptr(slicePtr) + CULLSize)))

		data := utils.BytesToSlice(sliceData, sliceLen)
		buf = append(buf, data...)
	}

	return uint64(filter.OnData(buf, endOfStream == 1))
}

//export envoyGoFilterOnDownstreamEvent
func envoyGoFilterOnDownstreamEvent(wrapper unsafe.Pointer, event int) {
	filter := DownstreamFilters.GetFilter(uint64(uintptr(wrapper)))
	e := api.ConnectionEvent(event)
	filter.OnEvent(e)
	if e == api.LocalClose || e == api.RemoteClose {
		DownstreamFilters.DeleteFilter(uint64(uintptr(wrapper)))
	}
}

//export envoyGoFilterOnDownstreamWrite
func envoyGoFilterOnDownstreamWrite(wrapper unsafe.Pointer, dataSize uint64, dataPtr uint64, sliceNum int, endOfStream int) uint64 {
	filter := DownstreamFilters.GetFilter(uint64(uintptr(wrapper)))

	var buf []byte

	for i := 0; i < sliceNum; i++ {
		slicePtr := dataPtr + uint64(i)*uint64(CULLSize+CULLSize)
		sliceData := *((*uint64)(unsafe.Pointer(uintptr(slicePtr))))
		sliceLen := *((*uint64)(unsafe.Pointer(uintptr(slicePtr) + CULLSize)))

		data := utils.BytesToSlice(sliceData, sliceLen)
		buf = append(buf, data...)
	}

	return uint64(filter.OnWrite(buf, endOfStream == 1))
}

//export envoyGoFilterOnSemaDec
func envoyGoFilterOnSemaDec(wrapper unsafe.Pointer) {
	w := DownstreamFilters.GetFilterWrapper(uint64(uintptr(wrapper)))
	if atomic.CompareAndSwapInt32(&w.cb.waitingOnEnvoy, 1, 0) {
		w.cb.sema.Done()
	}
}

//export envoyGoFilterOnUpstreamConnectionReady
func envoyGoFilterOnUpstreamConnectionReady(wrapper unsafe.Pointer, connID uint64) {
	cb := &connectionCallback{
		wrapper:                 wrapper,
		writeFunc:               cgoAPI.UpstreamWrite,
		closeFunc:               cgoAPI.UpstreamClose,
		infoFunc:                cgoAPI.UpstreamInfo,
		connEnableHalfCloseFunc: cgoAPI.UpstreamConnEnableHalfClose,
	}
	// switch filter from idMap to wrapperMap
	filter := UpstreamFilters.GetFilterByConnID(connID)
	UpstreamFilters.DeleteFilterByConnID(connID)
	UpstreamFilters.StoreFilterByWrapper(uint64(uintptr(wrapper)), filter)
	filter.OnPoolReady(cb)
}

//export envoyGoFilterOnUpstreamConnectionFailure
func envoyGoFilterOnUpstreamConnectionFailure(wrapper unsafe.Pointer, reason int, connID uint64) {
	filter := UpstreamFilters.GetFilterByConnID(connID)
	UpstreamFilters.DeleteFilterByConnID(connID)
	filter.OnPoolFailure(api.PoolFailureReason(reason), "")
}

//export envoyGoFilterOnUpstreamData
func envoyGoFilterOnUpstreamData(wrapper unsafe.Pointer, dataSize uint64, dataPtr uint64, sliceNum int, endOfStream int) {
	filter := UpstreamFilters.GetFilterByWrapper(uint64(uintptr(wrapper)))

	var buf []byte

	for i := 0; i < sliceNum; i++ {
		slicePtr := dataPtr + uint64(i)*uint64(CULLSize+CULLSize)
		sliceData := *((*uint64)(unsafe.Pointer(uintptr(slicePtr))))
		sliceLen := *((*uint64)(unsafe.Pointer(uintptr(slicePtr) + CULLSize)))

		data := utils.BytesToSlice(sliceData, sliceLen)
		buf = append(buf, data...)
	}

	filter.OnData(buf, endOfStream == 1)
}

//export envoyGoFilterOnUpstreamEvent
func envoyGoFilterOnUpstreamEvent(wrapper unsafe.Pointer, event int) {
	filter := UpstreamFilters.GetFilterByWrapper(uint64(uintptr(wrapper)))
	e := api.ConnectionEvent(event)
	filter.OnEvent(e)
	if e == api.LocalClose || e == api.RemoteClose {
		UpstreamFilters.DeleteFilterByWrapper(uint64(uintptr(wrapper)))
	}
}

type downstreamFilterWrapper struct {
	filter api.DownstreamFilter
	cb     *connectionCallback
}

type DownstreamFilterMap struct {
	m sync.Map // uint64 -> *downstreamFilterWrapper
}

func (f *DownstreamFilterMap) StoreFilter(key uint64, filter *downstreamFilterWrapper) error {
	if _, loaded := f.m.LoadOrStore(key, filter); loaded {
		return ErrDupRequestKey
	}
	return nil
}

func (f *DownstreamFilterMap) GetFilter(key uint64) api.DownstreamFilter {
	w := f.GetFilterWrapper(key)
	if w != nil {
		return w.filter
	}
	return nil
}

func (f *DownstreamFilterMap) GetFilterWrapper(key uint64) *downstreamFilterWrapper {
	if v, ok := f.m.Load(key); ok {
		return v.(*downstreamFilterWrapper)
	}
	return nil
}

func (f *DownstreamFilterMap) DeleteFilter(key uint64) {
	f.m.Delete(key)
}

func (f *DownstreamFilterMap) Clear() {
	f.m.Range(func(key, _ interface{}) bool {
		f.m.Delete(key)
		return true
	})
}

type UpstreamFilterMap struct {
	idMap      sync.Map // upstreamConnID(uint) -> UpstreamFilter
	wrapperMap sync.Map // wrapper(uint64) -> UpstreamFilter
}

func (f *UpstreamFilterMap) StoreFilterByConnID(key uint64, filter api.UpstreamFilter) error {
	if _, loaded := f.idMap.LoadOrStore(key, filter); loaded {
		return ErrDupRequestKey
	}
	return nil
}

func (f *UpstreamFilterMap) StoreFilterByWrapper(key uint64, filter api.UpstreamFilter) error {
	if _, loaded := f.wrapperMap.LoadOrStore(key, filter); loaded {
		return ErrDupRequestKey
	}
	return nil
}

func (f *UpstreamFilterMap) GetFilterByConnID(key uint64) api.UpstreamFilter {
	if v, ok := f.idMap.Load(key); ok {
		return v.(api.UpstreamFilter)
	}
	return nil
}

func (f *UpstreamFilterMap) GetFilterByWrapper(key uint64) api.UpstreamFilter {
	if v, ok := f.wrapperMap.Load(key); ok {
		return v.(api.UpstreamFilter)
	}
	return nil
}

func (f *UpstreamFilterMap) DeleteFilterByConnID(key uint64) {
	f.idMap.Delete(key)
}

func (f *UpstreamFilterMap) DeleteFilterByWrapper(key uint64) {
	f.wrapperMap.Delete(key)
}

func (f *UpstreamFilterMap) Clear() {
	f.idMap.Range(func(key, _ interface{}) bool {
		f.idMap.Delete(key)
		return true
	})
	f.wrapperMap.Range(func(key, _ interface{}) bool {
		f.wrapperMap.Delete(key)
		return true
	})
}
