package abi

/*
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include "../../../abi/abi.h"

typedef const envoy_dynamic_module_type_envoy_buffer* ConstEnvoyBufferPtr;
*/
import "C"
import (
	_ "embed"
	"fmt"
	"runtime"
	"strconv"
	"sync"
	"unsafe"

	cabi "github.com/envoyproxy/envoy/source/extensions/dynamic_modules/abi"
	sdk "github.com/envoyproxy/envoy/source/extensions/dynamic_modules/sdk/go"
	"github.com/envoyproxy/envoy/source/extensions/dynamic_modules/sdk/go/shared"
)

type httpFilterConfigWrapper struct {
	pluginFactory shared.HttpFilterFactory
}

type httpFilterConfigWrapperPerRoute struct {
	config any
}

type httpFilterWrapper = dymHttpFilterHandle

type httpFilterSharedDataWrapper struct {
	data any
}

const numManagerShards = 32

// The managers to keep track of configs and plugins.
type manager[T any] struct {
	data  [numManagerShards]map[uintptr]*T
	mutex [numManagerShards]sync.Mutex
}

func (m *manager[T]) record(item *T) unsafe.Pointer {
	pointer := unsafe.Pointer(item)
	index := uintptr(pointer) % numManagerShards
	m.mutex[index].Lock()
	defer m.mutex[index].Unlock()
	// Assume the map is initialized.
	m.data[index][uintptr(pointer)] = item
	return pointer
}

func (m *manager[T]) unwrap(itemPtr unsafe.Pointer) *T {
	return (*T)(itemPtr)
}

func (m *manager[T]) search(key uintptr) *T {
	index := key % numManagerShards
	m.mutex[index].Lock()
	defer m.mutex[index].Unlock()
	return m.data[index][key]
}

func (m *manager[T]) remove(itemPtr unsafe.Pointer) {
	index := uintptr(itemPtr) % numManagerShards
	m.mutex[index].Lock()
	defer m.mutex[index].Unlock()
	delete(m.data[index], uintptr(itemPtr))
}

func newManager[T any]() *manager[T] {
	m := &manager[T]{}
	for i := 0; i < numManagerShards; i++ {
		m.data[i] = make(map[uintptr]*T)
	}
	return m
}

var configManager = newManager[httpFilterConfigWrapper]()
var configPerRouteManager = newManager[httpFilterConfigWrapperPerRoute]()
var pluginManager = newManager[httpFilterWrapper]()
var sharedDataManager = newManager[httpFilterSharedDataWrapper]()

////////////////////////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////////////////

func nullModuleBuffer() C.envoy_dynamic_module_type_module_buffer {
	return C.envoy_dynamic_module_type_module_buffer{
		ptr:    nil,
		length: 0,
	}
}

func stringToModuleBuffer(str string) C.envoy_dynamic_module_type_module_buffer {
	return C.envoy_dynamic_module_type_module_buffer{
		ptr:    (*C.char)(unsafe.Pointer(unsafe.StringData(str))),
		length: C.size_t(len(str)),
	}
}

func bytesToModuleBuffer(b []byte) C.envoy_dynamic_module_type_module_buffer {
	return C.envoy_dynamic_module_type_module_buffer{
		ptr:    (*C.char)(unsafe.Pointer(unsafe.SliceData(b))),
		length: C.size_t(len(b)),
	}
}

func stringArrayToModuleBufferSlice(
	strs []string,
) []C.envoy_dynamic_module_type_module_buffer {
	views := make([]C.envoy_dynamic_module_type_module_buffer, len(strs))
	for i, str := range strs {
		views[i] = stringToModuleBuffer(str)
	}
	return views
}

func headersToModuleHttpHeaderSlice(
	headers [][2]string,
) []C.envoy_dynamic_module_type_module_http_header {
	views := make([]C.envoy_dynamic_module_type_module_http_header, len(headers))
	for i, header := range headers {
		views[i] = C.envoy_dynamic_module_type_module_http_header{
			key_ptr:      (*C.char)(unsafe.Pointer(unsafe.StringData(header[0]))),
			key_length:   C.size_t(len(header[0])),
			value_ptr:    (*C.char)(unsafe.Pointer(unsafe.StringData(header[1]))),
			value_length: C.size_t(len(header[1])),
		}
	}
	return views
}

func envoyBufferToStringUnsafe(buf C.envoy_dynamic_module_type_envoy_buffer) string {
	return unsafe.String((*byte)(unsafe.Pointer(buf.ptr)), buf.length)
}

func envoyBufferToBytesUnsafe(buf C.envoy_dynamic_module_type_envoy_buffer) []byte {
	return unsafe.Slice((*byte)(unsafe.Pointer(buf.ptr)), buf.length)
}

func envoyHttpHeaderSliceToHeadersUnsafe(
	buf []C.envoy_dynamic_module_type_envoy_http_header,
) [][2]string {
	headers := make([][2]string, len(buf))
	for i, header := range buf {
		key := unsafe.String((*byte)(unsafe.Pointer(header.key_ptr)), header.key_length)
		value := unsafe.String((*byte)(unsafe.Pointer(header.value_ptr)), header.value_length)
		headers[i] = [2]string{key, value}
	}
	return headers
}

func envoyBufferSliceToBytesSliceUnsafe(
	buf []C.envoy_dynamic_module_type_envoy_buffer,
) [][]byte {
	chunks := make([][]byte, 0, len(buf))
	for _, chunk := range buf {
		data := unsafe.Slice((*byte)(unsafe.Pointer(chunk.ptr)), chunk.length)
		chunks = append(chunks, data)
	}
	return chunks
}

func hostLog(level shared.LogLevel, format string, args []any) {
	logLevel := uint32(level)
	// Quick check if logging is enabled at this level.
	if !bool(C.envoy_dynamic_module_callback_log_enabled(
		(C.envoy_dynamic_module_type_log_level)(logLevel),
	)) {
		return
	}
	message := fmt.Sprintf(format, args...)
	C.envoy_dynamic_module_callback_log(
		(C.envoy_dynamic_module_type_log_level)(logLevel),
		stringToModuleBuffer(message),
	)
	runtime.KeepAlive(message)
}

type dymHeaderMap struct {
	hostPluginPtr C.envoy_dynamic_module_type_http_filter_envoy_ptr
	headerType    C.envoy_dynamic_module_type_http_header_type
}

func (h *dymHeaderMap) getSingleHeader(key string, index uint64, valueCount *uint64) string {
	var valueView C.envoy_dynamic_module_type_envoy_buffer
	ret := C.envoy_dynamic_module_callback_http_get_header(
		h.hostPluginPtr,
		h.headerType,
		stringToModuleBuffer(key),
		&valueView,
		(C.size_t)(index),
		(*C.size_t)(valueCount),
	)

	if !bool(ret) || valueView.ptr == nil || valueView.length == 0 {
		return ""
	}

	runtime.KeepAlive(key)
	return envoyBufferToStringUnsafe(valueView)
}

func (h *dymHeaderMap) Get(key string) []string {
	valueCount := uint64(0)

	firstValue := h.getSingleHeader(key, 0, &valueCount)
	if valueCount == 0 {
		return []string{}
	}

	values := make([]string, 0, valueCount)
	values = append(values, firstValue)

	for i := uint64(1); i < valueCount; i++ {
		value := h.getSingleHeader(key, i, nil)
		values = append(values, value)
	}

	return values
}

func (h *dymHeaderMap) GetOne(key string) string {
	return h.getSingleHeader(key, 0, nil)
}

func (h *dymHeaderMap) GetAll() [][2]string {
	headerCount := C.envoy_dynamic_module_callback_http_get_headers_size(
		(C.envoy_dynamic_module_type_http_filter_envoy_ptr)(h.hostPluginPtr),
		(C.envoy_dynamic_module_type_http_header_type)(h.headerType),
	)
	if headerCount == 0 {
		return nil
	}

	resultHeaders := make([]C.envoy_dynamic_module_type_envoy_http_header, headerCount)
	C.envoy_dynamic_module_callback_http_get_headers(
		(C.envoy_dynamic_module_type_http_filter_envoy_ptr)(h.hostPluginPtr),
		(C.envoy_dynamic_module_type_http_header_type)(h.headerType),
		unsafe.SliceData(resultHeaders),
	)
	finalResult := envoyHttpHeaderSliceToHeadersUnsafe(resultHeaders)
	runtime.KeepAlive(resultHeaders)
	return finalResult
}

func (h *dymHeaderMap) Set(key, value string) {
	C.envoy_dynamic_module_callback_http_set_header(
		(C.envoy_dynamic_module_type_http_filter_envoy_ptr)(h.hostPluginPtr),
		(C.envoy_dynamic_module_type_http_header_type)(h.headerType),
		stringToModuleBuffer(key),
		stringToModuleBuffer(value),
	)
	runtime.KeepAlive(key)
	runtime.KeepAlive(value)
}

func (h *dymHeaderMap) Add(key, value string) {
	C.envoy_dynamic_module_callback_http_add_header(
		(C.envoy_dynamic_module_type_http_filter_envoy_ptr)(h.hostPluginPtr),
		(C.envoy_dynamic_module_type_http_header_type)(h.headerType),
		stringToModuleBuffer(key),
		stringToModuleBuffer(value),
	)
	runtime.KeepAlive(key)
	runtime.KeepAlive(value)
}

func (h *dymHeaderMap) Remove(key string) {
	// The ABI use the set to nil to remove the header.
	C.envoy_dynamic_module_callback_http_set_header(
		(C.envoy_dynamic_module_type_http_filter_envoy_ptr)(h.hostPluginPtr),
		(C.envoy_dynamic_module_type_http_header_type)(h.headerType),
		stringToModuleBuffer(key),
		nullModuleBuffer(),
	)
	runtime.KeepAlive(key)
}

type dymBodyBuffer struct {
	hostPluginPtr C.envoy_dynamic_module_type_http_filter_envoy_ptr
	bufferType    C.envoy_dynamic_module_type_http_body_type
}

func (b *dymBodyBuffer) GetChunks() [][]byte {
	var chunksSize C.size_t = 0
	size := C.envoy_dynamic_module_callback_http_get_body_chunks_size(
		(C.envoy_dynamic_module_type_http_filter_envoy_ptr)(b.hostPluginPtr),
		(C.envoy_dynamic_module_type_http_body_type)(b.bufferType),
	)
	chunksSize = size
	if chunksSize == 0 {
		return nil
	}

	resultChunks := make([]C.envoy_dynamic_module_type_envoy_buffer, chunksSize)
	C.envoy_dynamic_module_callback_http_get_body_chunks(
		(C.envoy_dynamic_module_type_http_filter_envoy_ptr)(b.hostPluginPtr),
		(C.envoy_dynamic_module_type_http_body_type)(b.bufferType),
		unsafe.SliceData(resultChunks),
	)
	runtime.KeepAlive(resultChunks)
	return envoyBufferSliceToBytesSliceUnsafe(resultChunks)
}

func (b *dymBodyBuffer) GetSize() uint64 {
	size := C.envoy_dynamic_module_callback_http_get_body_size(
		b.hostPluginPtr,
		b.bufferType,
	)
	return uint64(size)
}

func (b *dymBodyBuffer) Append(data []byte) {
	if len(data) == 0 {
		return
	}
	C.envoy_dynamic_module_callback_http_append_body(
		b.hostPluginPtr,
		b.bufferType,
		bytesToModuleBuffer(data),
	)
	runtime.KeepAlive(data)
}

func (b *dymBodyBuffer) Drain(size uint64) {
	C.envoy_dynamic_module_callback_http_drain_body(
		b.hostPluginPtr,
		b.bufferType,
		(C.size_t)(size),
	)
}

type dymScheduler struct {
	schedulerPtr  C.envoy_dynamic_module_type_http_filter_scheduler_module_ptr
	schedulerLock sync.Mutex
	nextTaskID    uint64
	tasks         map[uint64]func()
}

func newDymScheduler(
	schedulerPtr C.envoy_dynamic_module_type_http_filter_scheduler_module_ptr,
) *dymScheduler {
	return &dymScheduler{
		schedulerPtr: schedulerPtr,
		tasks:        make(map[uint64]func()),
	}
}

func (s *dymScheduler) Schedule(task func()) {
	// Lock the scheduler to prevent concurrent access
	s.schedulerLock.Lock()
	taskID := s.nextTaskID
	s.nextTaskID++
	s.tasks[taskID] = task
	s.schedulerLock.Unlock()

	C.envoy_dynamic_module_callback_http_filter_scheduler_commit(
		s.schedulerPtr,
		(C.uint64_t)(taskID),
	)
}

func (s *dymScheduler) onScheduled(taskID uint64) {
	s.schedulerLock.Lock()
	task := s.tasks[taskID]
	delete(s.tasks, taskID)
	s.schedulerLock.Unlock()
	if task != nil {
		task()
	}
}

type dymHttpFilterHandle struct {
	hostPluginPtr C.envoy_dynamic_module_type_http_filter_envoy_ptr

	requestHeaderMap     dymHeaderMap
	responseHeaderMap    dymHeaderMap
	requestTrailerMap    dymHeaderMap
	responseTrailerMap   dymHeaderMap
	receivedRequestBody  dymBodyBuffer
	receivedResponseBody dymBodyBuffer
	bufferedRequestBody  dymBodyBuffer
	bufferedResponseBody dymBodyBuffer

	plugin            shared.HttpFilter
	scheduler         *dymScheduler
	streamCompleted   bool
	streamDestoried   bool
	localResponseSent bool
	// nextCalloutID was removed because callout ID is now returned by the host.

	calloutCallbacks map[uint64]shared.HttpCalloutCallback
	streamCallbacks  map[uint64]shared.HttpStreamCallback

	recordedSharedData []unsafe.Pointer

	downstreamWatermarkCallbacks shared.DownstreamWatermarkCallbacks
}

func (h *dymHttpFilterHandle) GetMetadataString(source shared.MetadataSourceType, metadataNamespace, key string) (string, bool) {
	var valueView C.envoy_dynamic_module_type_envoy_buffer

	ret := C.envoy_dynamic_module_callback_http_get_metadata_string(
		h.hostPluginPtr,
		(C.envoy_dynamic_module_type_metadata_source)(source),
		stringToModuleBuffer(metadataNamespace),
		stringToModuleBuffer(key),
		&valueView,
	)
	if !bool(ret) || valueView.ptr == nil || valueView.length == 0 {
		return "", false
	}

	runtime.KeepAlive(metadataNamespace)
	runtime.KeepAlive(key)
	return envoyBufferToStringUnsafe(valueView), true
}

func (h *dymHttpFilterHandle) GetMetadataNumber(source shared.MetadataSourceType, metadataNamespace, key string) (float64, bool) {
	var value C.double = 0

	ret := C.envoy_dynamic_module_callback_http_get_metadata_number(
		h.hostPluginPtr,
		(C.envoy_dynamic_module_type_metadata_source)(source),
		stringToModuleBuffer(metadataNamespace),
		stringToModuleBuffer(key),
		&value,
	)
	if !bool(ret) {
		return 0, false
	}

	runtime.KeepAlive(metadataNamespace)
	runtime.KeepAlive(key)
	return float64(value), true
}

func (h *dymHttpFilterHandle) SetMetadata(metadataNamespace, key string, value any) {
	var numValue float64 = 0
	var isNum bool = false
	var strValue string = ""
	var isStr bool = false

	switch v := value.(type) {
	case uint:
		numValue = float64(v)
		isNum = true
	case uint8:
		numValue = float64(v)
		isNum = true
	case uint16:
		numValue = float64(v)
		isNum = true
	case uint32:
		numValue = float64(v)
		isNum = true
	case uint64:
		numValue = float64(v)
		isNum = true
	case int:
		numValue = float64(v)
		isNum = true
	case int8:
		numValue = float64(v)
		isNum = true
	case int16:
		numValue = float64(v)
		isNum = true
	case int32:
		numValue = float64(v)
		isNum = true
	case int64:
		numValue = float64(v)
		isNum = true
	case float32:
		numValue = float64(v)
		isNum = true
	case float64:
		numValue = float64(v)
		isNum = true
	case string:
		strValue = v
		isStr = true
	}

	if isNum {
		C.envoy_dynamic_module_callback_http_set_dynamic_metadata_number(
			h.hostPluginPtr,
			stringToModuleBuffer(metadataNamespace),
			stringToModuleBuffer(key),
			(C.double)(numValue),
		)
	} else if isStr {
		C.envoy_dynamic_module_callback_http_set_dynamic_metadata_string(
			h.hostPluginPtr,
			stringToModuleBuffer(metadataNamespace),
			stringToModuleBuffer(key),
			stringToModuleBuffer(strValue),
		)
	}
	runtime.KeepAlive(metadataNamespace)
	runtime.KeepAlive(key)
	runtime.KeepAlive(strValue)
}

func (h *dymHttpFilterHandle) GetAttributeNumber(
	attributeID shared.AttributeID,
) (float64, bool) {
	var value C.uint64_t = 0

	ret := C.envoy_dynamic_module_callback_http_filter_get_attribute_int(
		h.hostPluginPtr,
		(C.envoy_dynamic_module_type_attribute_id)(attributeID),
		&value,
	)
	if !bool(ret) {
		return 0, false
	}

	return float64(value), true
}

func (h *dymHttpFilterHandle) GetAttributeString(
	attributeID shared.AttributeID,
) (string, bool) {
	var valueView C.envoy_dynamic_module_type_envoy_buffer

	ret := C.envoy_dynamic_module_callback_http_filter_get_attribute_string(
		h.hostPluginPtr,
		(C.envoy_dynamic_module_type_attribute_id)(attributeID),
		&valueView,
	)
	if !bool(ret) || valueView.ptr == nil || valueView.length == 0 {
		return "", false
	}

	return envoyBufferToStringUnsafe(valueView), true
}

func (h *dymHttpFilterHandle) GetFilterState(key string) ([]byte, bool) {
	var valueView C.envoy_dynamic_module_type_envoy_buffer

	ret := C.envoy_dynamic_module_callback_http_get_filter_state_bytes(
		h.hostPluginPtr,
		stringToModuleBuffer(key),
		&valueView,
	)
	if !bool(ret) || valueView.ptr == nil || valueView.length == 0 {
		return nil, false
	}

	runtime.KeepAlive(key)
	return envoyBufferToBytesUnsafe(valueView), true
}

func (h *dymHttpFilterHandle) SetFilterState(key string, value []byte) {
	C.envoy_dynamic_module_callback_http_set_filter_state_bytes(
		h.hostPluginPtr,
		stringToModuleBuffer(key),
		bytesToModuleBuffer(value),
	)
	runtime.KeepAlive(key)
	runtime.KeepAlive(value)
}

func (h *dymHttpFilterHandle) GetData(key string) any {
	stringValue, found := h.GetMetadataString(shared.MetadataSourceTypeDynamic,
		"composer.shared_data", key)
	if !found {
		return nil
	}
	// Convert string back to uintptr safely.
	uintValue, err := strconv.ParseUint(stringValue, 10, 64)
	if err != nil {
		return nil
	}
	pointer := uintptr(uintValue)
	// Use search rather than unwrap because the go runtime will complain
	// the pointer parsed from string `pointer arithmetic result points to invalid allocation`.
	wrapper := sharedDataManager.search(pointer)
	if wrapper == nil {
		return nil
	}
	return wrapper.data
}

func (h *dymHttpFilterHandle) SetData(key string, value any) {
	wrapper := &httpFilterSharedDataWrapper{data: value}
	pointer := sharedDataManager.record(wrapper)
	h.recordedSharedData = append(h.recordedSharedData, pointer)

	// Covert pointer to uintptr to string safely.
	stringValue := strconv.FormatUint(uint64(uintptr(pointer)), 10)
	h.SetMetadata("composer.shared_data", key, stringValue)
}

func (h *dymHttpFilterHandle) clearData() {
	for _, pointer := range h.recordedSharedData {
		sharedDataManager.remove(pointer)
	}
}

func (h *dymHttpFilterHandle) SendLocalResponse(
	statusCode uint32,
	headers [][2]string,
	body []byte,
	detail string,
) {
	h.localResponseSent = true

	// Prepare headers.
	headerViews := headersToModuleHttpHeaderSlice(headers)
	C.envoy_dynamic_module_callback_http_send_response(
		h.hostPluginPtr,
		(C.uint32_t)(statusCode),
		unsafe.SliceData(headerViews),
		(C.size_t)(len(headerViews)),
		bytesToModuleBuffer(body),
		stringToModuleBuffer(detail),
	)

	runtime.KeepAlive(body)
	runtime.KeepAlive(detail)
	runtime.KeepAlive(headers)
}

func (h *dymHttpFilterHandle) SendResponseHeaders(
	headers [][2]string, endOfStream bool,
) {
	// Prepare headers.
	headerViews := headersToModuleHttpHeaderSlice(headers)
	C.envoy_dynamic_module_callback_http_send_response_headers(
		h.hostPluginPtr,
		unsafe.SliceData(headerViews),
		(C.size_t)(len(headerViews)),
		(C.bool)(endOfStream),
	)
	runtime.KeepAlive(headers)
	runtime.KeepAlive(headerViews)
}

func (h *dymHttpFilterHandle) SendResponseData(
	data []byte, endOfStream bool,
) {
	C.envoy_dynamic_module_callback_http_send_response_data(
		h.hostPluginPtr,
		bytesToModuleBuffer(data),
		(C.bool)(endOfStream),
	)
	runtime.KeepAlive(data)
}

func (h *dymHttpFilterHandle) SendResponseTrailers(
	trailers [][2]string,
) {
	// Prepare trailers.
	trailerViews := headersToModuleHttpHeaderSlice(trailers)
	C.envoy_dynamic_module_callback_http_send_response_trailers(
		h.hostPluginPtr,
		unsafe.SliceData(trailerViews),
		(C.size_t)(len(trailerViews)),
	)
	runtime.KeepAlive(trailers)
	runtime.KeepAlive(trailerViews)
}

func (h *dymHttpFilterHandle) AddCustomFlag(flag string) {
	C.envoy_dynamic_module_callback_http_add_custom_flag(
		h.hostPluginPtr,
		stringToModuleBuffer(flag),
	)
}

func (h *dymHttpFilterHandle) ContinueRequest() {
	C.envoy_dynamic_module_callback_http_filter_continue_decoding(
		(C.envoy_dynamic_module_type_http_filter_envoy_ptr)(h.hostPluginPtr),
	)
}

func (h *dymHttpFilterHandle) ContinueResponse() {
	C.envoy_dynamic_module_callback_http_filter_continue_encoding(
		(C.envoy_dynamic_module_type_http_filter_envoy_ptr)(h.hostPluginPtr),
	)
}

func (h *dymHttpFilterHandle) ClearRouteCache() {
	C.envoy_dynamic_module_callback_http_clear_route_cache(h.hostPluginPtr)
}

func (h *dymHttpFilterHandle) RequestHeaders() shared.HeaderMap {
	return &h.requestHeaderMap
}

func (h *dymHttpFilterHandle) BufferedRequestBody() shared.BodyBuffer {
	return &h.bufferedRequestBody
}

func (h *dymHttpFilterHandle) RequestTrailers() shared.HeaderMap {
	return &h.requestTrailerMap
}

func (h *dymHttpFilterHandle) ResponseHeaders() shared.HeaderMap {
	return &h.responseHeaderMap
}

func (h *dymHttpFilterHandle) BufferedResponseBody() shared.BodyBuffer {
	return &h.bufferedResponseBody
}

func (h *dymHttpFilterHandle) ResponseTrailers() shared.HeaderMap {
	return &h.responseTrailerMap
}

func (h *dymHttpFilterHandle) GetMostSpecificConfig() any {
	perRoutePtr := C.envoy_dynamic_module_callback_get_most_specific_route_config(
		h.hostPluginPtr,
	)
	if perRoutePtr != nil {
		w := configPerRouteManager.unwrap(unsafe.Pointer(perRoutePtr))
		return w.config
	}
	return nil
}

func (h *dymHttpFilterHandle) GetScheduler() shared.Scheduler {
	if h.scheduler == nil {
		// The scheduler is created lazily and should never be nil
		// in practice. But it will be nil in mock tests.
		schedulerPtr := C.envoy_dynamic_module_callback_http_filter_scheduler_new(
			h.hostPluginPtr)
		h.scheduler = newDymScheduler(schedulerPtr)

		runtime.SetFinalizer(h.scheduler, func(s *dymScheduler) {
			C.envoy_dynamic_module_callback_http_filter_scheduler_delete(
				s.schedulerPtr,
			)
		})
	}
	return h.scheduler
}

func (h *dymHttpFilterHandle) Log(level shared.LogLevel, format string, args ...any) {
	hostLog(level, format, args)
}

func (h *dymHttpFilterHandle) HttpCallout(
	cluster string, headers [][2]string, body []byte, timeoutMs uint64,
	cb shared.HttpCalloutCallback) (shared.HttpCalloutInitResult, uint64) {
	// Prepare headers.
	headerViews := headersToModuleHttpHeaderSlice(headers)
	var calloutID C.uint64_t = 0

	result := C.envoy_dynamic_module_callback_http_filter_http_callout(
		h.hostPluginPtr,
		&calloutID,
		stringToModuleBuffer(cluster),
		unsafe.SliceData(headerViews),
		(C.size_t)(len(headerViews)),
		bytesToModuleBuffer(body),
		(C.uint64_t)(timeoutMs),
	)

	runtime.KeepAlive(cluster)
	runtime.KeepAlive(headers)
	runtime.KeepAlive(body)
	runtime.KeepAlive(headerViews)

	goResult := shared.HttpCalloutInitResult(result)
	if goResult != shared.HttpCalloutInitSuccess {
		return goResult, 0
	}

	if h.calloutCallbacks == nil {
		h.calloutCallbacks = make(map[uint64]shared.HttpCalloutCallback)
	}
	h.calloutCallbacks[uint64(calloutID)] = cb

	return goResult, uint64(calloutID)
}

func (h *dymHttpFilterHandle) StartHttpStream(
	cluster string, headers [][2]string, body []byte, endOfStream bool, timeoutMs uint64,
	cb shared.HttpStreamCallback) (shared.HttpCalloutInitResult, uint64) {
	// Prepare headers.
	headerViews := headersToModuleHttpHeaderSlice(headers)
	var streamID C.uint64_t = 0

	result := C.envoy_dynamic_module_callback_http_filter_start_http_stream(
		h.hostPluginPtr,
		&streamID,
		stringToModuleBuffer(cluster),
		unsafe.SliceData(headerViews),
		(C.size_t)(len(headerViews)),
		bytesToModuleBuffer(body),
		(C.bool)(endOfStream),
		(C.uint64_t)(timeoutMs),
	)

	runtime.KeepAlive(cluster)
	runtime.KeepAlive(headers)
	runtime.KeepAlive(body)
	runtime.KeepAlive(headerViews)

	goResult := shared.HttpCalloutInitResult(result)
	if goResult != shared.HttpCalloutInitSuccess {
		return goResult, 0
	}

	if h.streamCallbacks == nil {
		h.streamCallbacks = make(map[uint64]shared.HttpStreamCallback)
	}
	h.streamCallbacks[uint64(streamID)] = cb

	return goResult, uint64(streamID)
}

func (h *dymHttpFilterHandle) SendHttpStreamData(
	streamID uint64, data []byte, endOfStream bool,
) bool {
	ret := C.envoy_dynamic_module_callback_http_stream_send_data(
		h.hostPluginPtr,
		(C.uint64_t)(streamID),
		bytesToModuleBuffer(data),
		(C.bool)(endOfStream),
	)
	runtime.KeepAlive(data)
	return bool(ret)
}

func (h *dymHttpFilterHandle) SendHttpStreamTrailers(
	streamID uint64, trailers [][2]string,
) bool {
	// Prepare trailers.
	trailerViews := headersToModuleHttpHeaderSlice(trailers)
	ret := C.envoy_dynamic_module_callback_http_stream_send_trailers(
		h.hostPluginPtr,
		(C.uint64_t)(streamID),
		unsafe.SliceData(trailerViews),
		(C.size_t)(len(trailerViews)),
	)
	runtime.KeepAlive(trailers)
	runtime.KeepAlive(trailerViews)
	return bool(ret)
}

func (h *dymHttpFilterHandle) ResetHttpStream(
	streamID uint64,
) {
	C.envoy_dynamic_module_callback_http_filter_reset_http_stream(
		h.hostPluginPtr,
		(C.uint64_t)(streamID),
	)
}

func (h *dymHttpFilterHandle) SetDownstreamWatermarkCallbacks(
	cbs shared.DownstreamWatermarkCallbacks,
) {
	h.downstreamWatermarkCallbacks = cbs
}

func (h *dymHttpFilterHandle) ClearDownstreamWatermarkCallbacks() {
	h.downstreamWatermarkCallbacks = nil
}

func (h *dymHttpFilterHandle) RecordHistogramValue(id shared.MetricID,
	value uint64, tagsValues ...string) shared.MetricsResult {
	idUint64 := uint64(id)
	// Prepare tag values.
	tagValueViews := stringArrayToModuleBufferSlice(tagsValues)

	ret := C.envoy_dynamic_module_callback_http_filter_record_histogram_value(
		h.hostPluginPtr,
		(C.size_t)(idUint64),
		unsafe.SliceData(tagValueViews),
		(C.size_t)(len(tagValueViews)),
		(C.uint64_t)(value),
	)

	runtime.KeepAlive(tagsValues)
	runtime.KeepAlive(tagValueViews)
	return shared.MetricsResult(ret)
}

func (h *dymHttpFilterHandle) SetGaugeValue(id shared.MetricID,
	value uint64, tagsValues ...string) shared.MetricsResult {
	idUint64 := uint64(id)
	// Prepare tag values.
	tagValueViews := stringArrayToModuleBufferSlice(tagsValues)

	ret := C.envoy_dynamic_module_callback_http_filter_set_gauge(
		h.hostPluginPtr,
		(C.size_t)(idUint64),
		unsafe.SliceData(tagValueViews),
		(C.size_t)(len(tagValueViews)),
		(C.uint64_t)(value),
	)

	runtime.KeepAlive(tagsValues)
	runtime.KeepAlive(tagValueViews)
	return shared.MetricsResult(ret)
}

func (h *dymHttpFilterHandle) IncrementGaugeValue(id shared.MetricID,
	value uint64, tagsValues ...string) shared.MetricsResult {
	// Prepare tag values.
	tagValueViews := stringArrayToModuleBufferSlice(tagsValues)
	ret := C.envoy_dynamic_module_callback_http_filter_increment_gauge(
		h.hostPluginPtr,
		(C.size_t)(uint64(id)),
		unsafe.SliceData(tagValueViews),
		(C.size_t)(len(tagValueViews)),
		(C.uint64_t)(value),
	)
	runtime.KeepAlive(tagsValues)
	runtime.KeepAlive(tagValueViews)
	return shared.MetricsResult(ret)
}

func (h *dymHttpFilterHandle) DecrementGaugeValue(id shared.MetricID,
	value uint64, tagsValues ...string) shared.MetricsResult {
	// Prepare tag values.
	tagValueViews := stringArrayToModuleBufferSlice(tagsValues)
	ret := C.envoy_dynamic_module_callback_http_filter_decrement_gauge(
		h.hostPluginPtr,
		(C.size_t)(uint64(id)),
		unsafe.SliceData(tagValueViews),
		(C.size_t)(len(tagValueViews)),
		(C.uint64_t)(value),
	)
	runtime.KeepAlive(tagsValues)
	runtime.KeepAlive(tagValueViews)
	return shared.MetricsResult(ret)
}

func (h *dymHttpFilterHandle) IncrementCounterValue(id shared.MetricID,
	value uint64, tagsValues ...string) shared.MetricsResult {
	// Prepare tag values.
	tagValueViews := stringArrayToModuleBufferSlice(tagsValues)
	ret := C.envoy_dynamic_module_callback_http_filter_increment_counter(
		h.hostPluginPtr,
		(C.size_t)(uint64(id)),
		unsafe.SliceData(tagValueViews),
		(C.size_t)(len(tagValueViews)),
		(C.uint64_t)(value),
	)
	runtime.KeepAlive(tagsValues)
	runtime.KeepAlive(tagValueViews)
	return shared.MetricsResult(ret)
}

func newDymStreamPluginHandle(
	hostPluginPtr C.envoy_dynamic_module_type_http_filter_envoy_ptr,
) *dymHttpFilterHandle {
	pluginHandle := &dymHttpFilterHandle{
		hostPluginPtr: hostPluginPtr,
		requestHeaderMap: dymHeaderMap{
			hostPluginPtr: hostPluginPtr,
			headerType:    C.envoy_dynamic_module_type_http_header_type(0),
		},
		requestTrailerMap: dymHeaderMap{
			hostPluginPtr: hostPluginPtr,
			headerType:    C.envoy_dynamic_module_type_http_header_type(1),
		},
		responseHeaderMap: dymHeaderMap{
			hostPluginPtr: hostPluginPtr,
			headerType:    C.envoy_dynamic_module_type_http_header_type(2),
		},
		responseTrailerMap: dymHeaderMap{
			hostPluginPtr: hostPluginPtr,
			headerType:    C.envoy_dynamic_module_type_http_header_type(3),
		},
		receivedRequestBody: dymBodyBuffer{
			hostPluginPtr: hostPluginPtr,
			bufferType:    C.envoy_dynamic_module_type_http_body_type(0),
		},
		bufferedRequestBody: dymBodyBuffer{
			hostPluginPtr: hostPluginPtr,
			bufferType:    C.envoy_dynamic_module_type_http_body_type(1),
		},
		receivedResponseBody: dymBodyBuffer{
			hostPluginPtr: hostPluginPtr,
			bufferType:    C.envoy_dynamic_module_type_http_body_type(2),
		},
		bufferedResponseBody: dymBodyBuffer{
			hostPluginPtr: hostPluginPtr,
			bufferType:    C.envoy_dynamic_module_type_http_body_type(3),
		},
	}
	return pluginHandle
}

type dymConfigHandle struct {
	hostConfigPtr C.envoy_dynamic_module_type_http_filter_config_envoy_ptr
}

func (h *dymConfigHandle) Log(level shared.LogLevel, format string, args ...any) {
	hostLog(level, format, args)
}

func (h *dymConfigHandle) DefineHistogram(name string,
	tagKeys ...string) (shared.MetricID, shared.MetricsResult) {
	// Prepare tag keys.
	tagKeyViews := stringArrayToModuleBufferSlice(tagKeys)

	var metricID C.size_t = 0

	var tagKeyPtr *C.envoy_dynamic_module_type_module_buffer = nil
	if len(tagKeyViews) > 0 {
		tagKeyPtr = unsafe.SliceData(tagKeyViews)
	}

	result := C.envoy_dynamic_module_callback_http_filter_config_define_histogram(
		h.hostConfigPtr,
		stringToModuleBuffer(name),
		tagKeyPtr,
		(C.size_t)(len(tagKeyViews)),
		&metricID,
	)

	runtime.KeepAlive(name)
	runtime.KeepAlive(tagKeys)
	runtime.KeepAlive(tagKeyViews)
	return shared.MetricID(metricID), shared.MetricsResult(result)
}

func (h *dymConfigHandle) DefineGauge(name string,
	tagKeys ...string) (shared.MetricID, shared.MetricsResult) {
	// Prepare tag keys.
	tagKeyViews := stringArrayToModuleBufferSlice(tagKeys)

	var metricID C.size_t = 0
	var tagKeyPtr *C.envoy_dynamic_module_type_module_buffer = nil
	if len(tagKeyViews) > 0 {
		tagKeyPtr = unsafe.SliceData(tagKeyViews)
	}

	result := C.envoy_dynamic_module_callback_http_filter_config_define_gauge(
		h.hostConfigPtr,
		stringToModuleBuffer(name),
		tagKeyPtr,
		(C.size_t)(len(tagKeyViews)),
		&metricID,
	)

	runtime.KeepAlive(name)
	runtime.KeepAlive(tagKeys)
	runtime.KeepAlive(tagKeyViews)
	return shared.MetricID(metricID), shared.MetricsResult(result)
}

func (h *dymConfigHandle) DefineCounter(name string,
	tagKeys ...string) (shared.MetricID, shared.MetricsResult) {
	// Prepare tag keys.
	tagKeyViews := stringArrayToModuleBufferSlice(tagKeys)

	var metricID C.size_t = 0
	var tagKeyPtr *C.envoy_dynamic_module_type_module_buffer = nil
	if len(tagKeyViews) > 0 {
		tagKeyPtr = unsafe.SliceData(tagKeyViews)
	}

	result := C.envoy_dynamic_module_callback_http_filter_config_define_counter(
		h.hostConfigPtr,
		stringToModuleBuffer(name),
		tagKeyPtr,
		(C.size_t)(len(tagKeyViews)),
		&metricID,
	)

	runtime.KeepAlive(name)
	runtime.KeepAlive(tagKeys)
	runtime.KeepAlive(tagKeyViews)
	return shared.MetricID(metricID), shared.MetricsResult(result)
}

type dymRouteConfigHandle struct{}

func (h *dymRouteConfigHandle) Log(level shared.LogLevel, format string, args ...any) {
	hostLog(level, format, args)
}

func (h *dymRouteConfigHandle) DefineHistogram(name string,
	tagKeys ...string) (shared.MetricID, shared.MetricsResult) {
	return 0, shared.MetricsFrozen
}

func (h *dymRouteConfigHandle) DefineGauge(name string,
	tagKeys ...string) (shared.MetricID, shared.MetricsResult) {
	return 0, shared.MetricsFrozen
}

func (h *dymRouteConfigHandle) DefineCounter(name string,
	tagKeys ...string) (shared.MetricID, shared.MetricsResult) {
	return 0, shared.MetricsFrozen
}

//export envoy_dynamic_module_on_program_init
func envoy_dynamic_module_on_program_init() C.envoy_dynamic_module_type_abi_version_module_ptr {
	return C.envoy_dynamic_module_type_abi_version_module_ptr(
		unsafe.Pointer(unsafe.StringData(cabi.AbiHeaderVersion)))
}

//export envoy_dynamic_module_on_http_filter_config_new
func envoy_dynamic_module_on_http_filter_config_new(
	hostConfigPtr C.envoy_dynamic_module_type_http_filter_config_envoy_ptr,
	name C.envoy_dynamic_module_type_envoy_buffer,
	config C.envoy_dynamic_module_type_envoy_buffer,
) C.envoy_dynamic_module_type_http_filter_config_module_ptr {
	nameString := envoyBufferToStringUnsafe(name)
	configBytes := envoyBufferToBytesUnsafe(config)

	configHandle := &dymConfigHandle{hostConfigPtr: hostConfigPtr}
	factory, err := sdk.NewHttpFilterFactory(configHandle, nameString, configBytes)
	if err != nil || factory == nil {
		configHandle.Log(shared.LogLevelWarn, "Failed to load configuration: %v", err)
		return nil
	}
	configPtr := configManager.record(&httpFilterConfigWrapper{pluginFactory: factory})
	return C.envoy_dynamic_module_type_http_filter_config_module_ptr(configPtr)
}

//export envoy_dynamic_module_on_http_filter_config_destroy
func envoy_dynamic_module_on_http_filter_config_destroy(
	configPtr C.envoy_dynamic_module_type_http_filter_config_module_ptr,
) {
	configManager.remove(unsafe.Pointer(configPtr))
}

//export envoy_dynamic_module_on_http_filter_per_route_config_new
func envoy_dynamic_module_on_http_filter_per_route_config_new(
	name C.envoy_dynamic_module_type_envoy_buffer,
	config C.envoy_dynamic_module_type_envoy_buffer,
) C.envoy_dynamic_module_type_http_filter_per_route_config_module_ptr {
	nameStr := envoyBufferToStringUnsafe(name)
	configBytes := envoyBufferToBytesUnsafe(config)

	// The route config handle only make logging available.
	configHandle := &dymRouteConfigHandle{}

	configFactory := sdk.GetHttpFilterConfigFactory(nameStr)
	if configFactory == nil {
		configHandle.Log(shared.LogLevelWarn,
			"Failed to load configuration: no factory for %s", nameStr)
		return nil
	}
	parsedConfig, err := configFactory.CreatePerRoute(configBytes)
	if err != nil || parsedConfig == nil {
		configHandle.Log(shared.LogLevelWarn,
			"Failed to load per-route configuration: %v", err)
		return nil
	}

	configPtr := configPerRouteManager.record(&httpFilterConfigWrapperPerRoute{config: parsedConfig})
	return C.envoy_dynamic_module_type_http_filter_per_route_config_module_ptr(configPtr)
}

//export envoy_dynamic_module_on_http_filter_per_route_config_destroy
func envoy_dynamic_module_on_http_filter_per_route_config_destroy(
	configPtr C.envoy_dynamic_module_type_http_filter_per_route_config_module_ptr,
) {
	configPerRouteManager.remove(unsafe.Pointer(configPtr))
}

//export envoy_dynamic_module_on_http_filter_new
func envoy_dynamic_module_on_http_filter_new(
	pluginConfigPtr C.envoy_dynamic_module_type_http_filter_config_module_ptr,
	hostPluginPtr C.envoy_dynamic_module_type_http_filter_envoy_ptr,
) C.envoy_dynamic_module_type_http_filter_module_ptr {
	factoryWrapper := configManager.unwrap(unsafe.Pointer(pluginConfigPtr))
	if factoryWrapper == nil {
		return nil
	}

	// Create the plugin wrapper.

	pluginWrapper := newDymStreamPluginHandle(hostPluginPtr)
	pluginWrapper.plugin = factoryWrapper.pluginFactory.Create(pluginWrapper)
	pluginPtr := pluginManager.record(pluginWrapper)
	return C.envoy_dynamic_module_type_http_filter_module_ptr(pluginPtr)
}

//export envoy_dynamic_module_on_http_filter_destroy
func envoy_dynamic_module_on_http_filter_destroy(
	pluginPtr C.envoy_dynamic_module_type_http_filter_module_ptr,
) {
	pluginWrapper := pluginManager.unwrap(unsafe.Pointer(pluginPtr))
	if pluginWrapper == nil || pluginWrapper.streamDestoried {
		return
	}
	pluginWrapper.streamDestoried = true
	pluginManager.remove(unsafe.Pointer(pluginPtr))
}

//export envoy_dynamic_module_on_http_filter_request_headers
func envoy_dynamic_module_on_http_filter_request_headers(
	_ C.envoy_dynamic_module_type_http_filter_envoy_ptr,
	pluginPtr C.envoy_dynamic_module_type_http_filter_module_ptr,
	endOfStream C.bool,
) C.envoy_dynamic_module_type_on_http_filter_request_headers_status {
	// Get the plugin wrapper.
	pluginWrapper := pluginManager.unwrap(unsafe.Pointer(pluginPtr))
	if pluginWrapper == nil || pluginWrapper.plugin == nil {
		return 0
	}

	return C.envoy_dynamic_module_type_on_http_filter_request_headers_status(
		pluginWrapper.plugin.OnRequestHeaders(&pluginWrapper.requestHeaderMap, bool(endOfStream)))
}

//export envoy_dynamic_module_on_http_filter_request_body
func envoy_dynamic_module_on_http_filter_request_body(
	_ C.envoy_dynamic_module_type_http_filter_envoy_ptr,
	pluginPtr C.envoy_dynamic_module_type_http_filter_module_ptr,
	endOfStream C.bool,
) C.envoy_dynamic_module_type_on_http_filter_request_body_status {
	pluginWrapper := pluginManager.unwrap(unsafe.Pointer(pluginPtr))
	if pluginWrapper == nil || pluginWrapper.plugin == nil {
		return 0
	}
	return C.envoy_dynamic_module_type_on_http_filter_request_body_status(
		pluginWrapper.plugin.OnRequestBody(&pluginWrapper.receivedRequestBody, bool(endOfStream)))
}

//export envoy_dynamic_module_on_http_filter_request_trailers
func envoy_dynamic_module_on_http_filter_request_trailers(
	_ C.envoy_dynamic_module_type_http_filter_envoy_ptr,
	pluginPtr C.envoy_dynamic_module_type_http_filter_module_ptr,
) C.envoy_dynamic_module_type_on_http_filter_request_trailers_status {
	pluginWrapper := pluginManager.unwrap(unsafe.Pointer(pluginPtr))
	if pluginWrapper == nil || pluginWrapper.plugin == nil {
		return 0
	}
	return C.envoy_dynamic_module_type_on_http_filter_request_trailers_status(
		pluginWrapper.plugin.OnRequestTrailers(&pluginWrapper.requestTrailerMap))
}

//export envoy_dynamic_module_on_http_filter_response_headers
func envoy_dynamic_module_on_http_filter_response_headers(
	_ C.envoy_dynamic_module_type_http_filter_envoy_ptr,
	pluginPtr C.envoy_dynamic_module_type_http_filter_module_ptr,
	endOfStream C.bool,
) C.envoy_dynamic_module_type_on_http_filter_response_headers_status {
	pluginWrapper := pluginManager.unwrap(unsafe.Pointer(pluginPtr))
	if pluginWrapper == nil || pluginWrapper.plugin == nil || pluginWrapper.localResponseSent {
		return 0
	}
	return C.envoy_dynamic_module_type_on_http_filter_response_headers_status(
		pluginWrapper.plugin.OnResponseHeaders(&pluginWrapper.responseHeaderMap, bool(endOfStream)))
}

//export envoy_dynamic_module_on_http_filter_response_body
func envoy_dynamic_module_on_http_filter_response_body(
	_ C.envoy_dynamic_module_type_http_filter_envoy_ptr,
	pluginPtr C.envoy_dynamic_module_type_http_filter_module_ptr,
	endOfStream C.bool,
) C.envoy_dynamic_module_type_on_http_filter_response_body_status {
	pluginWrapper := pluginManager.unwrap(unsafe.Pointer(pluginPtr))
	if pluginWrapper == nil || pluginWrapper.plugin == nil || pluginWrapper.localResponseSent {
		return 0
	}
	return C.envoy_dynamic_module_type_on_http_filter_response_body_status(
		pluginWrapper.plugin.OnResponseBody(&pluginWrapper.receivedResponseBody, bool(endOfStream)))
}

//export envoy_dynamic_module_on_http_filter_response_trailers
func envoy_dynamic_module_on_http_filter_response_trailers(
	_ C.envoy_dynamic_module_type_http_filter_envoy_ptr,
	pluginPtr C.envoy_dynamic_module_type_http_filter_module_ptr,
) C.envoy_dynamic_module_type_on_http_filter_response_trailers_status {
	pluginWrapper := pluginManager.unwrap(unsafe.Pointer(pluginPtr))
	if pluginWrapper == nil || pluginWrapper.plugin == nil || pluginWrapper.localResponseSent {
		return 0
	}
	return C.envoy_dynamic_module_type_on_http_filter_response_trailers_status(
		pluginWrapper.plugin.OnResponseTrailers(&pluginWrapper.responseTrailerMap))
}

//export envoy_dynamic_module_on_http_filter_stream_complete
func envoy_dynamic_module_on_http_filter_stream_complete(
	_ C.envoy_dynamic_module_type_http_filter_envoy_ptr,
	pluginPtr C.envoy_dynamic_module_type_http_filter_module_ptr,
) {
	pluginWrapper := pluginManager.unwrap(unsafe.Pointer(pluginPtr))
	if pluginWrapper == nil || pluginWrapper.plugin == nil {
		return
	}
	pluginWrapper.streamCompleted = true
	pluginWrapper.clearData()
	pluginWrapper.scheduler = nil
	pluginWrapper.plugin.OnStreamComplete()
}

//export envoy_dynamic_module_on_http_filter_scheduled
func envoy_dynamic_module_on_http_filter_scheduled(
	_ C.envoy_dynamic_module_type_http_filter_envoy_ptr,
	pluginPtr C.envoy_dynamic_module_type_http_filter_module_ptr,
	taskID C.uint64_t,
) {
	pluginWrapper := pluginManager.unwrap(unsafe.Pointer(pluginPtr))
	if pluginWrapper == nil || pluginWrapper.scheduler == nil || pluginWrapper.streamCompleted {
		return
	}
	pluginWrapper.scheduler.onScheduled(uint64(taskID))
}

//export envoy_dynamic_module_on_http_filter_http_callout_done
func envoy_dynamic_module_on_http_filter_http_callout_done(
	_ C.envoy_dynamic_module_type_http_filter_envoy_ptr,
	pluginPtr C.envoy_dynamic_module_type_http_filter_module_ptr,
	calloutID C.uint64_t,
	result C.envoy_dynamic_module_type_http_callout_result,
	headers *C.envoy_dynamic_module_type_envoy_http_header,
	headersSize C.size_t,
	chunks *C.envoy_dynamic_module_type_envoy_buffer,
	chunksSize C.size_t,
) {
	pluginWrapper := pluginManager.unwrap(unsafe.Pointer(pluginPtr))
	if pluginWrapper == nil || pluginWrapper.streamCompleted {
		return
	}

	// Prepare headers and body chunks.
	resultHeaders := envoyHttpHeaderSliceToHeadersUnsafe(unsafe.Slice(headers, int(headersSize)))
	resultChunks := envoyBufferSliceToBytesSliceUnsafe(unsafe.Slice(chunks, int(chunksSize)))

	cb := pluginWrapper.calloutCallbacks[uint64(calloutID)]
	if cb != nil {
		delete(pluginWrapper.calloutCallbacks, uint64(calloutID))
		cb.OnHttpCalloutDone(uint64(calloutID),
			shared.HttpCalloutResult(result),
			resultHeaders,
			resultChunks,
		)
	}
}

//export envoy_dynamic_module_on_http_filter_http_stream_headers
func envoy_dynamic_module_on_http_filter_http_stream_headers(
	_ C.envoy_dynamic_module_type_http_filter_envoy_ptr,
	pluginPtr C.envoy_dynamic_module_type_http_filter_module_ptr,
	streamID C.uint64_t,
	headers *C.envoy_dynamic_module_type_envoy_http_header,
	headersSize C.size_t,
	endOfStream C.bool,
) {
	pluginWrapper := pluginManager.unwrap(unsafe.Pointer(pluginPtr))
	if pluginWrapper == nil || pluginWrapper.streamCompleted {
		return
	}

	// Prepare headers.
	resultHeaders := envoyHttpHeaderSliceToHeadersUnsafe(unsafe.Slice(headers, int(headersSize)))

	cb := pluginWrapper.streamCallbacks[uint64(streamID)]
	if cb != nil {
		cb.OnHttpStreamHeaders(uint64(streamID), resultHeaders, bool(endOfStream))
	}
}

//export envoy_dynamic_module_on_http_filter_http_stream_data
func envoy_dynamic_module_on_http_filter_http_stream_data(
	_ C.envoy_dynamic_module_type_http_filter_envoy_ptr,
	pluginPtr C.envoy_dynamic_module_type_http_filter_module_ptr,
	streamID C.uint64_t,
	chunks C.ConstEnvoyBufferPtr,
	chunksSize C.size_t,
	endOfStream C.bool,
) {
	pluginWrapper := pluginManager.unwrap(unsafe.Pointer(pluginPtr))
	if pluginWrapper == nil || pluginWrapper.streamCompleted {
		return
	}

	// Prepare data.
	resultData := envoyBufferSliceToBytesSliceUnsafe(unsafe.Slice(chunks, int(chunksSize)))

	cb := pluginWrapper.streamCallbacks[uint64(streamID)]
	if cb != nil {
		cb.OnHttpStreamData(uint64(streamID), resultData, bool(endOfStream))
	}
}

//export envoy_dynamic_module_on_http_filter_http_stream_trailers
func envoy_dynamic_module_on_http_filter_http_stream_trailers(
	_ C.envoy_dynamic_module_type_http_filter_envoy_ptr,
	pluginPtr C.envoy_dynamic_module_type_http_filter_module_ptr,
	streamID C.uint64_t,
	trailers *C.envoy_dynamic_module_type_envoy_http_header,
	trailersSize C.size_t,
) {
	pluginWrapper := pluginManager.unwrap(unsafe.Pointer(pluginPtr))
	if pluginWrapper == nil || pluginWrapper.streamCompleted {
		return
	}

	// Prepare trailers.
	resultTrailers := envoyHttpHeaderSliceToHeadersUnsafe(unsafe.Slice(trailers, int(trailersSize)))

	cb := pluginWrapper.streamCallbacks[uint64(streamID)]
	if cb != nil {
		cb.OnHttpStreamTrailers(uint64(streamID), resultTrailers)
	}
}

//export envoy_dynamic_module_on_http_filter_http_stream_complete
func envoy_dynamic_module_on_http_filter_http_stream_complete(
	_ C.envoy_dynamic_module_type_http_filter_envoy_ptr,
	pluginPtr C.envoy_dynamic_module_type_http_filter_module_ptr,
	streamID C.uint64_t,
) {
	pluginWrapper := pluginManager.unwrap(unsafe.Pointer(pluginPtr))
	if pluginWrapper == nil || pluginWrapper.streamCompleted {
		return
	}

	cb := pluginWrapper.streamCallbacks[uint64(streamID)]
	if cb != nil {
		delete(pluginWrapper.streamCallbacks, uint64(streamID))
		cb.OnHttpStreamComplete(uint64(streamID))
	}
}

//export envoy_dynamic_module_on_http_filter_http_stream_reset
func envoy_dynamic_module_on_http_filter_http_stream_reset(
	_ C.envoy_dynamic_module_type_http_filter_envoy_ptr,
	pluginPtr C.envoy_dynamic_module_type_http_filter_module_ptr,
	streamID C.uint64_t,
	reason C.envoy_dynamic_module_type_http_stream_reset_reason,
) {
	pluginWrapper := pluginManager.unwrap(unsafe.Pointer(pluginPtr))
	if pluginWrapper == nil || pluginWrapper.streamCompleted {
		return
	}

	cb := pluginWrapper.streamCallbacks[uint64(streamID)]
	if cb != nil {
		delete(pluginWrapper.streamCallbacks, uint64(streamID))
		cb.OnHttpStreamReset(uint64(streamID), shared.HttpStreamResetReason(reason))
	}
}

//export envoy_dynamic_module_on_http_filter_downstream_above_write_buffer_high_watermark
func envoy_dynamic_module_on_http_filter_downstream_above_write_buffer_high_watermark(
	_ C.envoy_dynamic_module_type_http_filter_envoy_ptr,
	pluginPtr C.envoy_dynamic_module_type_http_filter_module_ptr,
) {
	pluginWrapper := pluginManager.unwrap(unsafe.Pointer(pluginPtr))
	if pluginWrapper == nil || pluginWrapper.streamCompleted {
		return
	}

	if pluginWrapper.downstreamWatermarkCallbacks != nil {
		pluginWrapper.downstreamWatermarkCallbacks.OnAboveWriteBufferHighWatermark()
	}
}

//export envoy_dynamic_module_on_http_filter_downstream_below_write_buffer_low_watermark
func envoy_dynamic_module_on_http_filter_downstream_below_write_buffer_low_watermark(
	_ C.envoy_dynamic_module_type_http_filter_envoy_ptr,
	pluginPtr C.envoy_dynamic_module_type_http_filter_module_ptr,
) {
	pluginWrapper := pluginManager.unwrap(unsafe.Pointer(pluginPtr))
	if pluginWrapper == nil || pluginWrapper.streamCompleted {
		return
	}

	if pluginWrapper.downstreamWatermarkCallbacks != nil {
		pluginWrapper.downstreamWatermarkCallbacks.OnBelowWriteBufferLowWatermark()
	}
}

//export envoy_dynamic_module_on_http_filter_local_reply
func envoy_dynamic_module_on_http_filter_local_reply(
	filter_envoy_ptr C.envoy_dynamic_module_type_http_filter_envoy_ptr,
	filter_module_ptr C.envoy_dynamic_module_type_http_filter_module_ptr,
	response_code C.uint32_t,
	details C.envoy_dynamic_module_type_envoy_buffer,
	reset_imminent C.bool,
) C.envoy_dynamic_module_type_on_http_filter_local_reply_status {
	return C.envoy_dynamic_module_type_on_http_filter_local_reply_status(0)
}
