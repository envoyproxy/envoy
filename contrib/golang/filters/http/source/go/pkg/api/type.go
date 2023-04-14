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

	// ByteSize return size of HeaderMap
	ByteSize() uint64
}

type RequestHeaderMap interface {
	HeaderMap
	Protocol() string
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

	// Peek returns n bytes from buffer, without draining any buffered data.
	// If n > readable buffer, nil will be returned.
	// It can be used in codec to check first-n-bytes magic bytes
	// Note: do not change content in return bytes, use write instead
	Peek(n int) []byte

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
