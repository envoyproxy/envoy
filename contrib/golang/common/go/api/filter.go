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

package api

import "google.golang.org/protobuf/types/known/anypb"

type (
	// PassThroughStreamEncoderFilter provides the no-op implementation of the StreamEncoderFilter interface.
	PassThroughStreamEncoderFilter struct{}
	// PassThroughStreamDecoderFilter provides the no-op implementation of the StreamDecoderFilter interface.
	PassThroughStreamDecoderFilter struct{}
	// PassThroughStreamFilter provides the no-op implementation of the StreamFilter interface.
	PassThroughStreamFilter struct {
		PassThroughStreamDecoderFilter
		PassThroughStreamEncoderFilter
	}

	// EmptyDownstreamFilter provides the no-op implementation of the DownstreamFilter interface
	EmptyDownstreamFilter struct{}
	// EmptyUpstreamFilter provides the no-op implementation of the UpstreamFilter interface
	EmptyUpstreamFilter struct{}
)

// request
type StreamDecoderFilter interface {
	DecodeHeaders(RequestHeaderMap, bool) StatusType
	DecodeData(BufferInstance, bool) StatusType
	DecodeTrailers(RequestTrailerMap) StatusType
}

func (*PassThroughStreamDecoderFilter) DecodeHeaders(RequestHeaderMap, bool) StatusType {
	return Continue
}

func (*PassThroughStreamDecoderFilter) DecodeData(BufferInstance, bool) StatusType {
	return Continue
}

func (*PassThroughStreamDecoderFilter) DecodeTrailers(RequestTrailerMap) StatusType {
	return Continue
}

// response
type StreamEncoderFilter interface {
	EncodeHeaders(ResponseHeaderMap, bool) StatusType
	EncodeData(BufferInstance, bool) StatusType
	EncodeTrailers(ResponseTrailerMap) StatusType
}

func (*PassThroughStreamEncoderFilter) EncodeHeaders(ResponseHeaderMap, bool) StatusType {
	return Continue
}

func (*PassThroughStreamEncoderFilter) EncodeData(BufferInstance, bool) StatusType {
	return Continue
}

func (*PassThroughStreamEncoderFilter) EncodeTrailers(ResponseTrailerMap) StatusType {
	return Continue
}

type StreamFilter interface {
	// http request
	StreamDecoderFilter
	// response stream
	StreamEncoderFilter

	// log
	OnLog(RequestHeaderMap, RequestTrailerMap, ResponseHeaderMap, ResponseTrailerMap)
	OnLogDownstreamStart(RequestHeaderMap)
	OnLogDownstreamPeriodic(RequestHeaderMap, RequestTrailerMap, ResponseHeaderMap, ResponseTrailerMap)

	// destroy filter
	OnDestroy(DestroyReason)
	OnStreamComplete()
}

func (*PassThroughStreamFilter) OnLog(RequestHeaderMap, RequestTrailerMap, ResponseHeaderMap, ResponseTrailerMap) {
}

func (*PassThroughStreamFilter) OnLogDownstreamStart(RequestHeaderMap) {
}

func (*PassThroughStreamFilter) OnLogDownstreamPeriodic(RequestHeaderMap, RequestTrailerMap, ResponseHeaderMap, ResponseTrailerMap) {
}

func (*PassThroughStreamFilter) OnDestroy(DestroyReason) {
}

func (*PassThroughStreamFilter) OnStreamComplete() {
}

type StreamFilterConfigParser interface {
	// Parse the proto message to any Go value, and return error to reject the config.
	// This is called when Envoy receives the config from the control plane.
	// Also, you can define Metrics through the callbacks, and the callbacks will be nil when parsing the route config.
	Parse(any *anypb.Any, callbacks ConfigCallbackHandler) (interface{}, error)
	// Merge the two configs(filter level config or route level config) into one.
	// May merge multi-level configurations, i.e. filter level, virtualhost level, router level and weighted cluster level,
	// into a single one recursively, by invoking this method multiple times.
	Merge(parentConfig interface{}, childConfig interface{}) interface{}
}

type StreamFilterFactory func(config interface{}, callbacks FilterCallbackHandler) StreamFilter

// stream info
// refer https://github.com/envoyproxy/envoy/blob/main/envoy/stream_info/stream_info.h
type StreamInfo interface {
	GetRouteName() string
	FilterChainName() string
	// Protocol return the request's protocol.
	Protocol() (string, bool)
	// ResponseCode return the response code.
	ResponseCode() (uint32, bool)
	// ResponseCodeDetails return the response code details.
	ResponseCodeDetails() (string, bool)
	// AttemptCount return the number of times the request was attempted upstream.
	AttemptCount() uint32
	// Get the dynamic metadata of the request
	DynamicMetadata() DynamicMetadata
	// DownstreamLocalAddress return the downstream local address.
	DownstreamLocalAddress() string
	// DownstreamRemoteAddress return the downstream remote address.
	DownstreamRemoteAddress() string
	// UpstreamLocalAddress return the upstream local address.
	UpstreamLocalAddress() (string, bool)
	// UpstreamRemoteAddress return the upstream remote address.
	UpstreamRemoteAddress() (string, bool)
	// UpstreamClusterName return the upstream host cluster.
	UpstreamClusterName() (string, bool)
	// FilterState return the filter state interface.
	FilterState() FilterState
	// VirtualClusterName returns the name of the virtual cluster which got matched
	VirtualClusterName() (string, bool)
	// WorkerID returns the ID of the Envoy worker thread
	WorkerID() uint32
	// Some fields in stream info can be fetched via GetProperty
	// For example, startTime() is equal to GetProperty("request.time")
}

type StreamFilterCallbacks interface {
	StreamInfo() StreamInfo

	// ClearRouteCache clears the route cache for the current request, and filtermanager will re-fetch the route in the next filter.
	// Please be careful to invoke it, since filtermanager will raise an 404 route_not_found response when failed to re-fetch a route.
	ClearRouteCache()
	Log(level LogType, msg string)
	LogLevel() LogType
	// GetProperty fetch Envoy attribute and return the value as a string.
	// The list of attributes can be found in https://www.envoyproxy.io/docs/envoy/latest/intro/arch_overview/advanced/attributes.
	// If the fetch succeeded, a string will be returned.
	// If the value is a timestamp, it is returned as a timestamp string like "2023-07-31T07:21:40.695646+00:00".
	// If the fetch failed (including the value is not found), an error will be returned.
	//
	// The error can be one of:
	// * ErrInternalFailure
	// * ErrSerializationFailure (Currently, fetching attributes in List/Map type are unsupported)
	// * ErrValueNotFound
	GetProperty(key string) (string, error)
	// TODO add more for filter callbacks
}

// FilterProcessCallbacks is the interface for filter to process request/response in decode/encode phase.
type FilterProcessCallbacks interface {
	// Continue or SendLocalReply should be last API invoked, no more code after them.
	Continue(StatusType)
	SendLocalReply(responseCode int, bodyText string, headers map[string][]string, grpcStatus int64, details string)
	// RecoverPanic recover panic in defer and terminate the request by SendLocalReply with 500 status code.
	RecoverPanic()
}

type DecoderFilterCallbacks interface {
	FilterProcessCallbacks
}

type EncoderFilterCallbacks interface {
	FilterProcessCallbacks
}

type FilterCallbackHandler interface {
	StreamFilterCallbacks
	// DecoderFilterCallbacks could only be used in DecodeXXX phases.
	DecoderFilterCallbacks() DecoderFilterCallbacks
	// EncoderFilterCallbacks could only be used in EncodeXXX phases.
	EncoderFilterCallbacks() EncoderFilterCallbacks
}

type DynamicMetadata interface {
	Get(filterName string) map[string]interface{}
	Set(filterName string, key string, value interface{})
}

type DownstreamFilter interface {
	// Called when a connection is first established.
	OnNewConnection() FilterStatus
	// Called when data is read on the connection.
	OnData(buffer []byte, endOfStream bool) FilterStatus
	// Callback for connection events.
	OnEvent(event ConnectionEvent)
	// Called when data is to be written on the connection.
	OnWrite(buffer []byte, endOfStream bool) FilterStatus
}

func (*EmptyDownstreamFilter) OnNewConnection() FilterStatus {
	return NetworkFilterContinue
}

func (*EmptyDownstreamFilter) OnData(buffer []byte, endOfStream bool) FilterStatus {
	return NetworkFilterContinue
}

func (*EmptyDownstreamFilter) OnEvent(event ConnectionEvent) {
}

func (*EmptyDownstreamFilter) OnWrite(buffer []byte, endOfStream bool) FilterStatus {
	return NetworkFilterContinue
}

type UpstreamFilter interface {
	// Called when a connection is available to process a request/response.
	OnPoolReady(cb ConnectionCallback)
	// Called when a pool error occurred and no connection could be acquired for making the request.
	OnPoolFailure(poolFailureReason PoolFailureReason, transportFailureReason string)
	// Invoked when data is delivered from the upstream connection.
	OnData(buffer []byte, endOfStream bool)
	// Callback for connection events.
	OnEvent(event ConnectionEvent)
}

func (*EmptyUpstreamFilter) OnPoolReady(cb ConnectionCallback) {
}

func (*EmptyUpstreamFilter) OnPoolFailure(poolFailureReason PoolFailureReason, transportFailureReason string) {
}

func (*EmptyUpstreamFilter) OnData(buffer []byte, endOfStream bool) FilterStatus {
	return NetworkFilterContinue
}

func (*EmptyUpstreamFilter) OnEvent(event ConnectionEvent) {
}

type ConnectionCallback interface {
	// StreamInfo returns the stream info of the connection
	StreamInfo() StreamInfo
	// Write data to the connection.
	Write(buffer []byte, endStream bool)
	// Close the connection.
	Close(closeType ConnectionCloseType)
	// EnableHalfClose only for upstream connection
	EnableHalfClose(enabled bool)
}

type StateType int

const (
	StateTypeReadOnly StateType = 0
	StateTypeMutable  StateType = 1
)

type LifeSpan int

const (
	LifeSpanFilterChain LifeSpan = 0
	LifeSpanRequest     LifeSpan = 1
	LifeSpanConnection  LifeSpan = 2
	LifeSpanTopSpan     LifeSpan = 3
)

type StreamSharing int

const (
	None                             StreamSharing = 0
	SharedWithUpstreamConnection     StreamSharing = 1
	SharedWithUpstreamConnectionOnce StreamSharing = 2
)

type FilterState interface {
	SetString(key, value string, stateType StateType, lifeSpan LifeSpan, streamSharing StreamSharing)
	GetString(key string) string
}

type MetricType uint32

const (
	Counter   MetricType = 0
	Gauge     MetricType = 1
	Histogram MetricType = 2
)

type ConfigCallbacks interface {
	// Define a metric, for different MetricType, name must be different,
	// for same MetricType, the same name will share a metric.
	DefineCounterMetric(name string) CounterMetric
	DefineGaugeMetric(name string) GaugeMetric
	// TODO Histogram
}

type ConfigCallbackHandler interface {
	ConfigCallbacks
}

type CounterMetric interface {
	Increment(offset int64)
	Get() uint64
	Record(value uint64)
}

type GaugeMetric interface {
	Increment(offset int64)
	Get() uint64
	Record(value uint64)
}

// TODO
type HistogramMetric interface {
}
