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
	// Get is safe, but may low performance
	Get(name string) string
	// GetRaw is unsafe, reuse the memory from Envoy
	GetRaw(name string) string
	// override header
	Set(name, value string)
	Remove(name string)
	/*
		byteSize() uint64
		// append header
		AddCopy(name, value string)
	*/
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

// *************** BufferInstance start **************//
// refer https://github.com/envoyproxy/envoy/blob/main/envoy/buffer/buffer.h
type BufferInstance interface {
	/*
		CopyOut(start uint64, p []byte) int
		GetRawSlices() []byte
	*/
	Set(string)
	GetString() string
	Length() uint64
	// Send() // send to next filter
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

const (
	DecodeHeaderPhase int = 1
	DecodeDataPhase   int = 2
	DecodeTailerPhase int = 3
	EncodeHeaderPhase int = 4
	EncodeDataPhase   int = 5
	EncodeTailerPhase int = 6
)
