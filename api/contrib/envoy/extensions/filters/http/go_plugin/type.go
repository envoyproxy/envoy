package plugin

//****************** filter status start ******************//
type StatusType int

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

//****************** log level start ******************//
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

//****************** HeaderMap start ******************//
// refer https://github.com/envoyproxy/envoy/blob/main/envoy/http/header_map.h
type HeaderMap interface {
	Get(name string) string
	byteSize() uint64
	// append header
	AddCopy(name, value string)
	// override header
	SetCopy(name, value string)
	Remove(name string)
}

type RequestHeaderMap interface {
	HeaderMap
	// others
}

type RequestTrailerMap interface {
	HeaderMap
	// others
}

type ResponseHeaderMap interface {
	HeaderMap
	// others
}

type ResponseTrailerMap interface {
	HeaderMap
	// others
}

type MetadataMap interface {
}

//****************** HeaderMap end ******************//

//*************** BufferInstance start **************//
// refer https://github.com/envoyproxy/envoy/blob/main/envoy/buffer/buffer.h
type BufferInstance interface {
	CopyOut(start uint64, p []byte) int
	GetRawSlices() []byte
	ToString() string
	Length() uint64
}

//*************** BufferInstance end **************//
