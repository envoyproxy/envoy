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
	"reflect"
	"runtime"
	"unsafe"

	"mosn.io/envoy-go-extension/pkg/http/api"
)

type httpCApiImpl struct{}

func (c *httpCApiImpl) HttpContinue(r unsafe.Pointer, status uint64) {
	C.moeHttpContinue(r, C.int(status))
}

func (c *httpCApiImpl) HttpSendLocalReply(r unsafe.Pointer, response_code int, body_text string, headers map[string]string, grpc_status int64, details string) {
	hLen := len(headers)
	strs := make([]string, 0, hLen)
	for k, v := range headers {
		strs = append(strs, k, v)
	}
	C.moeHttpSendLocalReply(r, C.int(response_code), unsafe.Pointer(&body_text), unsafe.Pointer(&strs), C.longlong(grpc_status), unsafe.Pointer(&details))
}

func (c *httpCApiImpl) HttpGetHeader(r unsafe.Pointer, key *string, value *string) {
	C.moeHttpGetHeader(r, unsafe.Pointer(key), unsafe.Pointer(value))
}

func (c *httpCApiImpl) HttpCopyHeaders(r unsafe.Pointer, num uint64, bytes uint64) map[string]string {
	// TODO: use a memory pool for better performance,
	// since these go strings in strs, will be copied into the following map.
	strs := make([]string, num*2)
	// but, this buffer can not be reused safely,
	// since strings may refer to this buffer as string data, and string is const in go.
	// we have to make sure the all strings is not using before reusing,
	// but strings may be alive beyond the request life.
	buf := make([]byte, bytes)
	sHeader := (*reflect.SliceHeader)(unsafe.Pointer(&strs))
	bHeader := (*reflect.SliceHeader)(unsafe.Pointer(&buf))

	C.moeHttpCopyHeaders(r, unsafe.Pointer(sHeader.Data), unsafe.Pointer(bHeader.Data))

	m := make(map[string]string, num)
	for i := uint64(0); i < num*2; i += 2 {
		key := strs[i]
		value := strs[i+1]
		m[key] = value
		// fmt.Printf("value of %s: %s\n", key, value)
	}
	runtime.KeepAlive(buf)
	return m
}

func (c *httpCApiImpl) HttpSetHeader(r unsafe.Pointer, key *string, value *string) {
	C.moeHttpSetHeader(r, unsafe.Pointer(key), unsafe.Pointer(value))
}

func (c *httpCApiImpl) HttpRemoveHeader(r unsafe.Pointer, key *string) {
	C.moeHttpRemoveHeader(r, unsafe.Pointer(key))
}

func (c *httpCApiImpl) HttpGetBuffer(r unsafe.Pointer, bufferPtr uint64, value *string, length uint64) {
	buf := make([]byte, length)
	bHeader := (*reflect.SliceHeader)(unsafe.Pointer(&buf))
	sHeader := (*reflect.StringHeader)(unsafe.Pointer(value))
	sHeader.Data = bHeader.Data
	sHeader.Len = int(length)
	C.moeHttpGetBuffer(r, C.ulonglong(bufferPtr), unsafe.Pointer(bHeader.Data))
}

func (c *httpCApiImpl) HttpSetBuffer(r unsafe.Pointer, bufferPtr uint64, value string) {
	sHeader := (*reflect.StringHeader)(unsafe.Pointer(&value))
	C.moeHttpSetBuffer(r, C.ulonglong(bufferPtr), unsafe.Pointer(sHeader.Data), C.int(sHeader.Len))
}

func (c *httpCApiImpl) HttpCopyTrailers(r unsafe.Pointer, num uint64, bytes uint64) map[string]string {
	// TODO: use a memory pool for better performance,
	// but, should be very careful, since string is const in go,
	// and we have to make sure the strings is not using before reusing,
	// strings may be alive beyond the request life.
	strs := make([]string, num*2)
	buf := make([]byte, bytes)
	sHeader := (*reflect.SliceHeader)(unsafe.Pointer(&strs))
	bHeader := (*reflect.SliceHeader)(unsafe.Pointer(&buf))

	C.moeHttpCopyTrailers(r, unsafe.Pointer(sHeader.Data), unsafe.Pointer(bHeader.Data))

	m := make(map[string]string, num)
	for i := uint64(0); i < num*2; i += 2 {
		key := strs[i]
		value := strs[i+1]
		m[key] = value
	}
	return m
}

func (c *httpCApiImpl) HttpSetTrailer(r unsafe.Pointer, key *string, value *string) {
	C.moeHttpSetTrailer(r, unsafe.Pointer(key), unsafe.Pointer(value))
}

func (c *httpCApiImpl) HttpFinalize(r unsafe.Pointer, reason int) {
	C.moeHttpFinalize(r, C.int(reason))
}

var cAPI api.HttpCAPI = &httpCApiImpl{}

// SetHttpCAPI for mock cAPI
func SetHttpCAPI(api api.HttpCAPI) {
	cAPI = api
}
