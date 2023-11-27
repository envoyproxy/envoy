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

import "errors"

// ****************** filter status start ******************//
type StatusType int

const (
	Running                StatusType = 0
	LocalReply             StatusType = 1
	Continue               StatusType = 2
	StopAndBuffer          StatusType = 3
	StopAndBufferWatermark StatusType = 4
	StopNoBuffer           StatusType = 5
)

// header status
// refer https://github.com/envoyproxy/envoy/blob/main/envoy/http/filter.h
const (
	HeaderContinue                     StatusType = 100
	HeaderStopIteration                StatusType = 101
	HeaderContinueAndDontEndStream     StatusType = 102
	HeaderStopAllIterationAndBuffer    StatusType = 103
	HeaderStopAllIterationAndWatermark StatusType = 104
)

// data status
// refer https://github.com/envoyproxy/envoy/blob/main/envoy/http/filter.h
const (
	DataContinue                  StatusType = 200
	DataStopIterationAndBuffer    StatusType = 201
	DataStopIterationAndWatermark StatusType = 202
	DataStopIterationNoBuffer     StatusType = 203
)

// Trailer status
// refer https://github.com/envoyproxy/envoy/blob/main/envoy/http/filter.h
const (
	TrailerContinue      StatusType = 300
	TrailerStopIteration StatusType = 301
)

//****************** filter status end ******************//

// ****************** log level start ******************//
type LogType int

// refer https://github.com/envoyproxy/envoy/blob/main/source/common/common/base_logger.h
const (
	Trace    LogType = 0
	Debug    LogType = 1
	Info     LogType = 2
	Warn     LogType = 3
	Error    LogType = 4
	Critical LogType = 5
)

func (self LogType) String() string {
	switch self {
	case Trace:
		return "trace"
	case Debug:
		return "debug"
	case Info:
		return "info"
	case Warn:
		return "warn"
	case Error:
		return "error"
	case Critical:
		return "critical"
	}
	return "unknown"
}

//******************* log level end *******************//

// ****************** HeaderMap start ******************//

// refer https://github.com/envoyproxy/envoy/blob/main/envoy/http/header_map.h
type HeaderMap interface {
	// GetRaw is unsafe, reuse the memory from Envoy
	GetRaw(name string) string

	// Get value of key
	// If multiple values associated with this key, first one will be returned.
	Get(key string) (string, bool)

	// Values returns all values associated with the given key.
	// The returned slice is not a copy.
	Values(key string) []string

	// Set key-value pair in header map, the previous pair will be replaced if exists.
	// It may not take affects immediately in the Envoy thread side when it's invoked in a Go thread.
	Set(key, value string)

	// Add value for given key.
	// Multiple headers with the same key may be added with this function.
	// Use Set for setting a single header for the given key.
	// It may not take affects immediately in the Envoy thread side when it's invoked in a Go thread.
	Add(key, value string)

	// Del delete pair of specified key
	// It may not take affects immediately in the Envoy thread side when it's invoked in a Go thread.
	Del(key string)

	// Range calls f sequentially for each key and value present in the map.
	// If f returns false, range stops the iteration.
	// When there are multiple values of a key, f will be invoked multiple times with the same key and each value.
	Range(f func(key, value string) bool)

	// RangeWithCopy calls f sequentially for each key and value copied from the map.
	RangeWithCopy(f func(key, value string) bool)
}

type RequestHeaderMap interface {
	HeaderMap
	Scheme() string
	Method() string
	Host() string
	Path() string
}

type RequestTrailerMap interface {
	HeaderMap
	// others
}

type ResponseHeaderMap interface {
	HeaderMap
	Status() (int, bool)
}

type ResponseTrailerMap interface {
	HeaderMap
	// others
}

type MetadataMap interface {
}

//****************** HeaderMap end ******************//

// *************** BufferInstance start **************//
type BufferAction int

const (
	SetBuffer     BufferAction = 0
	AppendBuffer  BufferAction = 1
	PrependBuffer BufferAction = 2
)

type DataBufferBase interface {
	// Write appends the contents of p to the buffer, growing the buffer as
	// needed. The return value n is the length of p; err is always nil. If the
	// buffer becomes too large, Write will panic with ErrTooLarge.
	Write(p []byte) (n int, err error)

	// WriteString appends the string to the buffer, growing the buffer as
	// needed. The return value n is the length of s; err is always nil. If the
	// buffer becomes too large, Write will panic with ErrTooLarge.
	WriteString(s string) (n int, err error)

	// WriteByte appends the byte to the buffer, growing the buffer as
	// needed. The return value n is the length of s; err is always nil. If the
	// buffer becomes too large, Write will panic with ErrTooLarge.
	WriteByte(p byte) error

	// WriteUint16 appends the uint16 to the buffer, growing the buffer as
	// needed. The return value n is the length of s; err is always nil. If the
	// buffer becomes too large, Write will panic with ErrTooLarge.
	WriteUint16(p uint16) error

	// WriteUint32 appends the uint32 to the buffer, growing the buffer as
	// needed. The return value n is the length of s; err is always nil. If the
	// buffer becomes too large, Write will panic with ErrTooLarge.
	WriteUint32(p uint32) error

	// WriteUint64 appends the uint64 to the buffer, growing the buffer as
	// needed. The return value n is the length of s; err is always nil. If the
	// buffer becomes too large, Write will panic with ErrTooLarge.
	WriteUint64(p uint64) error

	// Bytes returns all bytes from buffer, without draining any buffered data.
	// It can be used to get fixed-length content, such as headers, body.
	// Note: do not change content in return bytes, use write instead
	Bytes() []byte

	// Drain drains a offset length of bytes in buffer.
	// It can be used with Bytes(), after consuming a fixed-length of data
	Drain(offset int)

	// Len returns the number of bytes of the unread portion of the buffer;
	// b.Len() == len(b.Bytes()).
	Len() int

	// Reset resets the buffer to be empty.
	Reset()

	// String returns the contents of the buffer as a string.
	String() string

	// Append append the contents of the slice data to the buffer.
	Append(data []byte) error
}

type BufferInstance interface {
	DataBufferBase

	// Set overwrite the whole buffer content with byte slice.
	Set([]byte) error

	// SetString overwrite the whole buffer content with string.
	SetString(string) error

	// Prepend prepend the contents of the slice data to the buffer.
	Prepend(data []byte) error

	// Prepend prepend the contents of the string data to the buffer.
	PrependString(s string) error

	// Append append the contents of the string data to the buffer.
	AppendString(s string) error
}

//*************** BufferInstance end **************//

type DestroyReason int

const (
	Normal    DestroyReason = 0
	Terminate DestroyReason = 1
)

// For each AccessLogType's meaning, see
// https://www.envoyproxy.io/docs/envoy/latest/configuration/observability/access_log/usage
// Currently, only some downstream access log types are supported
type AccessLogType int

const (
	AccessLogNotSet                                  AccessLogType = 0
	AccessLogTcpUpstreamConnected                    AccessLogType = 1
	AccessLogTcpPeriodic                             AccessLogType = 2
	AccessLogTcpConnectionEnd                        AccessLogType = 3
	AccessLogDownstreamStart                         AccessLogType = 4
	AccessLogDownstreamPeriodic                      AccessLogType = 5
	AccessLogDownstreamEnd                           AccessLogType = 6
	AccessLogUpstreamPoolReady                       AccessLogType = 7
	AccessLogUpstreamPeriodic                        AccessLogType = 8
	AccessLogUpstreamEnd                             AccessLogType = 9
	AccessLogDownstreamTunnelSuccessfullyEstablished AccessLogType = 10
)

const (
	NormalFinalize int = 0 // normal, finalize on destroy
	GCFinalize     int = 1 // finalize in GC sweep
)

type EnvoyRequestPhase int

const (
	DecodeHeaderPhase EnvoyRequestPhase = iota + 1
	DecodeDataPhase
	DecodeTrailerPhase
	EncodeHeaderPhase
	EncodeDataPhase
	EncodeTrailerPhase
)

func (e EnvoyRequestPhase) String() string {
	switch e {
	case DecodeHeaderPhase:
		return "DecodeHeader"
	case DecodeDataPhase:
		return "DecodeData"
	case DecodeTrailerPhase:
		return "DecodeTrailer"
	case EncodeHeaderPhase:
		return "EncodeHeader"
	case EncodeDataPhase:
		return "EncodeData"
	case EncodeTrailerPhase:
		return "EncodeTrailer"
	}
	return "unknown phase"
}

// Status codes returned by filters that can cause future filters to not get iterated to.
type FilterStatus int

const (
	// Continue to further filters.
	NetworkFilterContinue FilterStatus = 0
	// Stop executing further filters.
	NetworkFilterStopIteration FilterStatus = 1
)

func (s FilterStatus) String() string {
	switch s {
	case NetworkFilterContinue:
		return "Continue"
	case NetworkFilterStopIteration:
		return "StopIteration"
	}
	return "unknown"
}

// Events that occur on a connection.
type ConnectionEvent int

const (
	RemoteClose      ConnectionEvent = 0
	LocalClose       ConnectionEvent = 1
	Connected        ConnectionEvent = 2
	ConnectedZeroRtt ConnectionEvent = 3
)

func (e ConnectionEvent) String() string {
	switch e {
	case RemoteClose:
		return "RemoteClose"
	case LocalClose:
		return "LocalClose"
	case Connected:
		return "Connected"
	case ConnectedZeroRtt:
		return "ConnectedZeroRtt"
	}
	return "unknown"
}

// Type of connection close to perform.
type ConnectionCloseType int

const (
	// Flush pending write data before raising ConnectionEvent::LocalClose
	FlushWrite ConnectionCloseType = 0
	// Do not flush any pending data. Write the pending data to buffer and then immediately
	// raise ConnectionEvent::LocalClose
	NoFlush ConnectionCloseType = 1
	// Flush pending write data and delay raising a ConnectionEvent::LocalClose
	// until the delayed_close_timeout expires
	FlushWriteAndDelay ConnectionCloseType = 2
	// Do not write/flush any pending data and immediately raise ConnectionEvent::LocalClose
	Abort ConnectionCloseType = 3
	// Do not write/flush any pending data and immediately raise
	// ConnectionEvent::LocalClose. Envoy will try to close the connection with RST flag.
	AbortReset ConnectionCloseType = 4
)

func (t ConnectionCloseType) String() string {
	switch t {
	case FlushWrite:
		return "FlushWrite"
	case NoFlush:
		return "NoFlush"
	case FlushWriteAndDelay:
		return "FlushWriteAndDelay"
	case Abort:
		return "Abort"
	case AbortReset:
		return "AbortReset"
	}
	return "unknown"
}

type PoolFailureReason int

const (
	// A resource overflowed and policy prevented a new connection from being created.
	Overflow PoolFailureReason = 0
	// A local connection failure took place while creating a new connection.
	LocalConnectionFailure PoolFailureReason = 1
	// A remote connection failure took place while creating a new connection.
	RemoteConnectionFailure PoolFailureReason = 2
	// A timeout occurred while creating a new connection.
	Timeout PoolFailureReason = 3
)

func (r PoolFailureReason) String() string {
	switch r {
	case Overflow:
		return "Overflow"
	case LocalConnectionFailure:
		return "LocalConnectionFailure"
	case RemoteConnectionFailure:
		return "RemoteConnectionFailure"
	case Timeout:
		return "Timeout"
	}
	return "unknown"
}

type ConnectionInfoType int

const (
	ConnectionInfoLocalAddr  ConnectionInfoType = 0
	ConnectionInfoRemoteAddr ConnectionInfoType = 1
)

func (t ConnectionInfoType) String() string {
	switch t {
	case ConnectionInfoLocalAddr:
		return "ConnectionInfoLocalAddr"
	case ConnectionInfoRemoteAddr:
		return "ConnectionInfoRemoteAddr"
	}
	return "unknown"
}

// *************** errors start **************//
var (
	ErrInternalFailure = errors.New("internal failure")
	ErrValueNotFound   = errors.New("value not found")
	// Failed to serialize the value when we fetch the value as string
	ErrSerializationFailure = errors.New("serialization failure")
)

// *************** errors end **************//
