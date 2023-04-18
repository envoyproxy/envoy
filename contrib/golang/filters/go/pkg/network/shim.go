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

#cgo CFLAGS: -I../api
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

	"github.com/envoyproxy/envoy/contrib/golang/common/go/utils"
	"github.com/envoyproxy/envoy/contrib/golang/filters/go/pkg/api"
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

	libraryID string
)

func CreateUpstreamConn(addr string, filter api.UpstreamFilter) {
	h := uint64(uintptr(cgoAPI.UpstreamConnect(libraryID, addr)))

	// NP: make sure filter will be deleted.
	runtime.SetFinalizer(filter, func(f api.UpstreamFilter) {
		runtime.SetFinalizer(filter, func(f api.UpstreamFilter) {
			cgoAPI.UpstreamFinalize(unsafe.Pointer(uintptr(h)), api.NormalFinalize)
		})
	})

	// TODO: handle error
	_ = UpstreamFilters.StoreFilter(h, filter)
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
	}
	filter := filterFactory.CreateFilter(cb)

	// NP: make sure filter will be deleted.
	runtime.SetFinalizer(filter, func(ff api.DownstreamFilter) {
		cgoAPI.DownstreamFinalize(unsafe.Pointer(uintptr(wrapper)), api.NormalFinalize)
	})

	// TODO: handle error
	_ = DownstreamFilters.StoreFilter(uint64(uintptr(wrapper)), filter)

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
	filter.OnEvent(api.ConnectionEvent(event))
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

//export envoyGoFilterOnUpstreamConnectionReady
func envoyGoFilterOnUpstreamConnectionReady(wrapper unsafe.Pointer) {
	cb := &connectionCallback{
		wrapper:   wrapper,
		writeFunc: cgoAPI.UpstreamWrite,
		closeFunc: cgoAPI.UpstreamClose,
		infoFunc:  cgoAPI.UpstreamInfo,
	}
	filter := UpstreamFilters.GetFilter(uint64(uintptr(wrapper)))
	filter.OnPoolReady(cb)
}

//export envoyGoFilterOnUpstreamConnectionFailure
func envoyGoFilterOnUpstreamConnectionFailure(wrapper unsafe.Pointer, reason int) {
	filter := UpstreamFilters.GetFilter(uint64(uintptr(wrapper)))
	filter.OnPoolFailure(api.PoolFailureReason(reason), "")
	UpstreamFilters.DeleteFilter(uint64(uintptr(wrapper)))
}

//export envoyGoFilterOnUpstreamData
func envoyGoFilterOnUpstreamData(wrapper unsafe.Pointer, dataSize uint64, dataPtr uint64, sliceNum int, endOfStream int) {
	filter := UpstreamFilters.GetFilter(uint64(uintptr(wrapper)))

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
	filter := UpstreamFilters.GetFilter(uint64(uintptr(wrapper)))
	filter.OnEvent(api.ConnectionEvent(event))
}

type DownstreamFilterMap struct {
	m sync.Map // uint64 -> DownstreamFilter
}

func (f *DownstreamFilterMap) StoreFilter(key uint64, filter api.DownstreamFilter) error {
	if _, loaded := f.m.LoadOrStore(key, filter); loaded {
		return ErrDupRequestKey
	}
	return nil
}

func (f *DownstreamFilterMap) GetFilter(key uint64) api.DownstreamFilter {
	if v, ok := f.m.Load(key); ok {
		return v.(api.DownstreamFilter)
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
	m sync.Map // uint64 -> UpstreamFilter
}

func (f *UpstreamFilterMap) StoreFilter(key uint64, filter api.UpstreamFilter) error {
	if _, loaded := f.m.LoadOrStore(key, filter); loaded {
		return ErrDupRequestKey
	}
	return nil
}

func (f *UpstreamFilterMap) GetFilter(key uint64) api.UpstreamFilter {
	if v, ok := f.m.Load(key); ok {
		return v.(api.UpstreamFilter)
	}
	return nil
}

func (f *UpstreamFilterMap) DeleteFilter(key uint64) {
	f.m.Delete(key)
}

func (f *UpstreamFilterMap) Clear() {
	f.m.Range(func(key, _ interface{}) bool {
		f.m.Delete(key)
		return true
	})
}
