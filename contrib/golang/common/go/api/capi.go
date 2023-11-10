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

import "unsafe"

type HttpCAPI interface {
	HttpContinue(r unsafe.Pointer, status uint64)
	HttpSendLocalReply(r unsafe.Pointer, responseCode int, bodyText string, headers map[string]string, grpcStatus int64, details string)

	// Send a specialized reply that indicates that the filter has failed on the go side. Internally this is used for
	// when unhandled panics are detected.
	HttpSendPanicReply(r unsafe.Pointer, details string)
	// experience api, memory unsafe
	HttpGetHeader(r unsafe.Pointer, key string) string
	HttpCopyHeaders(r unsafe.Pointer, num uint64, bytes uint64) map[string][]string
	HttpSetHeader(r unsafe.Pointer, key string, value string, add bool)
	HttpRemoveHeader(r unsafe.Pointer, key string)

	HttpGetBuffer(r unsafe.Pointer, bufferPtr uint64, length uint64) []byte
	HttpDrainBuffer(r unsafe.Pointer, bufferPtr uint64, length uint64)
	HttpSetBufferHelper(r unsafe.Pointer, bufferPtr uint64, value string, action BufferAction)
	HttpSetBytesBufferHelper(r unsafe.Pointer, bufferPtr uint64, value []byte, action BufferAction)

	HttpCopyTrailers(r unsafe.Pointer, num uint64, bytes uint64) map[string][]string
	HttpSetTrailer(r unsafe.Pointer, key string, value string, add bool)
	HttpRemoveTrailer(r unsafe.Pointer, key string)

	HttpGetStringValue(r unsafe.Pointer, id int) (string, bool)
	HttpGetIntegerValue(r unsafe.Pointer, id int) (uint64, bool)

	HttpGetDynamicMetadata(r unsafe.Pointer, filterName string) map[string]interface{}
	HttpSetDynamicMetadata(r unsafe.Pointer, filterName string, key string, value interface{})

	HttpLog(level LogType, message string)
	HttpLogLevel() LogType

	HttpFinalize(r unsafe.Pointer, reason int)
	HttpConfigFinalize(c unsafe.Pointer)

	HttpSetStringFilterState(r unsafe.Pointer, key string, value string, stateType StateType, lifeSpan LifeSpan, streamSharing StreamSharing)
	HttpGetStringFilterState(r unsafe.Pointer, key string) string

	HttpGetStringProperty(r unsafe.Pointer, key string) (string, error)

	HttpDefineMetric(c unsafe.Pointer, metricType MetricType, name string) uint32
	HttpIncrementMetric(c unsafe.Pointer, metricId uint32, offset int64)
	HttpGetMetric(c unsafe.Pointer, metricId uint32) uint64
	HttpRecordMetric(c unsafe.Pointer, metricId uint32, value uint64)
}

type NetworkCAPI interface {
	// DownstreamWrite writes buffer data into downstream connection.
	DownstreamWrite(f unsafe.Pointer, bufferPtr unsafe.Pointer, bufferLen int, endStream int)
	// DownstreamClose closes the downstream connection
	DownstreamClose(f unsafe.Pointer, closeType int)
	// DownstreamFinalize cleans up the resource of downstream connection, should be called only by runtime.SetFinalizer
	DownstreamFinalize(f unsafe.Pointer, reason int)
	// DownstreamInfo gets the downstream connection info of infoType
	DownstreamInfo(f unsafe.Pointer, infoType int) string
	// GetFilterState gets the filter state of key
	GetFilterState(f unsafe.Pointer, key string) string
	// SetFilterState sets the filter state of key to value
	SetFilterState(f unsafe.Pointer, key string, value string, stateType StateType, lifeSpan LifeSpan, streamSharing StreamSharing)

	// UpstreamConnect creates an envoy upstream connection to address
	UpstreamConnect(libraryID string, addr string, connID uint64) unsafe.Pointer
	// UpstreamWrite writes buffer data into upstream connection.
	UpstreamWrite(f unsafe.Pointer, bufferPtr unsafe.Pointer, bufferLen int, endStream int)
	// UpstreamClose closes the upstream connection
	UpstreamClose(f unsafe.Pointer, closeType int)
	// UpstreamFinalize cleans up the resource of upstream connection, should be called only by runtime.SetFinalizer
	UpstreamFinalize(f unsafe.Pointer, reason int)
	// UpstreamInfo gets the upstream connection info of infoType
	UpstreamInfo(f unsafe.Pointer, infoType int) string
}

type CommonCAPI interface {
	Log(level LogType, message string)
	LogLevel() LogType
}

type commonCApiImpl struct{}

var cAPI CommonCAPI = &commonCApiImpl{}

// SetCommonCAPI for mock cAPI
func SetCommonCAPI(api CommonCAPI) {
	cAPI = api
}
