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
	"runtime"

	"google.golang.org/protobuf/proto"
	"google.golang.org/protobuf/types/known/anypb"
	"mosn.io/envoy-go-extension/pkg/http/api"
	"mosn.io/envoy-go-extension/pkg/utils"
)

var configNum uint64
var configCache = make(map[uint64]*anypb.Any, 16)

//export moeNewHttpPluginConfig
func moeNewHttpPluginConfig(configPtr uint64, configLen uint64) uint64 {
	buf := utils.BytesToSlice(configPtr, configLen)
	var any anypb.Any
	proto.Unmarshal(buf, &any)
	configNum++
	configCache[configNum] = &any
	return configNum
}

//export moeDestoryHttpPluginConfig
func moeDestoryHttpPluginConfig(id uint64) {
	delete(configCache, id)
}

var Requests = make(map[*C.httpRequest]*httpRequest, 64)

func requestFinalize(r *httpRequest) {
	r.Finalize(api.NormalFinalize)
}

func createRequest(r *C.httpRequest) *httpRequest {
	req := &httpRequest{
		req: r,
	}
	// NP: make sure filter will be deleted.
	runtime.SetFinalizer(req, requestFinalize)

	if _, ok := Requests[r]; ok {
		// TODO: error
	}
	Requests[r] = req

	configId := uint64(r.configId)
	filterFactory := getOrCreateHttpFilterFactory(configId)
	f := filterFactory(req)
	req.httpFilter = f

	return req
}

func getRequest(r *C.httpRequest) *httpRequest {
	req, ok := Requests[r]
	if !ok {
		// TODO: error
	}
	return req
}

//export moeOnHttpHeader
func moeOnHttpHeader(r *C.httpRequest, endStream, headerNum, headerBytes uint64) uint64 {
	var req *httpRequest
	phase := int(r.phase)
	if phase == api.DecodeHeaderPhase {
		req = createRequest(r)
	} else {
		req = getRequest(r)
		// early sendLocalReply may skip the whole decode phase
		if req == nil {
			req = createRequest(r)
		}
	}
	f := req.httpFilter

	header := &httpHeaderMap{
		request:     req,
		headerNum:   headerNum,
		headerBytes: headerBytes,
		isTrailer:   phase == api.DecodeTailerPhase || phase == api.EncodeTailerPhase,
	}

	var status api.StatusType
	switch phase {
	case api.DecodeHeaderPhase:
		status = f.DecodeHeaders(header, endStream == 1)
	case api.DecodeTailerPhase:
		status = f.DecodeTrailers(header)
	case api.EncodeHeaderPhase:
		status = f.EncodeHeaders(header, endStream == 1)
	case api.EncodeTailerPhase:
		status = f.EncodeTrailers(header)
	}
	// f.Callbacks().Continue(status)
	return uint64(status)
}

//export moeOnHttpData
func moeOnHttpData(r *C.httpRequest, endStream, buffer, length uint64) uint64 {
	req := getRequest(r)

	f := req.httpFilter
	isDecode := int(r.phase) == api.DecodeDataPhase

	buf := &httpBuffer{
		request:   req,
		bufferPtr: buffer,
		length:    length,
	}
	/*
		id := ""
		if hf, ok := f.(*httpFilter); ok {
			id = hf.config.AsMap()["id"].(string)
		}
		fmt.Printf("id: %s, buffer ptr: %p, buffer data: %s\n", id, buffer, buf.GetString())
	*/
	var status api.StatusType
	if isDecode {
		status = f.DecodeData(buf, endStream == 1)
	} else {
		status = f.EncodeData(buf, endStream == 1)
	}
	// f.Callbacks().Continue(status)
	return uint64(status)
}

//export moeOnHttpDestroy
func moeOnHttpDestroy(r *C.httpRequest, reason uint64) {
	req := getRequest(r)

	v := api.DestroyReason(reason)

	f := req.httpFilter
	f.OnDestroy(v)

	Requests[r] = nil

	// no one is using req now, we can remove it manually, for better performance.
	if v == api.Normal {
		runtime.SetFinalizer(req, nil)
		req.Finalize(api.GCFinalize)
	}
}
