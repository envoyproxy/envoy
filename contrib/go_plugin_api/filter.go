package plugin

// Stream Filter
// refer https://github.com/envoyproxy/envoy/blob/main/envoy/http/filter.h
type StreamFilter interface {
	// request stream
	StreamDecoderFilter
	// response stream
	StreamEncoderFilter
	// stream complete
	OnStreamComplete()
	// error log
	Log(LogType, string)
	// destroy filter
	OnDestroy()
}

// request
type StreamDecoderFilter interface {
	DecodeHeaders(RequestHeaderMap, bool) StatusType
	DecodeData(BufferInstance, bool) StatusType
	DecodeTrailers(RequestTrailerMap) StatusType
	DecodeMetadata(MetadataMap) StatusType
	DecoderCallbacks() DecoderFilterCallbacks
}

// response
type StreamEncoderFilter interface {
	EncodeHeaders(ResponseHeaderMap, bool) StatusType
	EncodeData(BufferInstance, bool) StatusType
	EncodeTrailers(ResponseTrailerMap) StatusType
	EncodeMetadata(MetadataMap) StatusType
	EncoderCallbacks() EncoderFilterCallbacks
}

// stream info
// refer https://github.com/envoyproxy/envoy/blob/main/envoy/stream_info/stream_info.h
type StreamInfo interface {
	GetRouteName() string
	VirtualClusterName() string
	BytesReceived() int64
	BytesSent() int64
	Protocol() string
	ResponseCode() int
	GetRequestHeaders() RequestHeaderMap
	ResponseCodeDetails() string
}

type StreamFilterCallbacks interface {
	StreamInfo() StreamInfo
}

type DecoderFilterCallbacks interface {
	StreamFilterCallbacks
	ContinueDecoding()
	AddDecodedData(buffer BufferInstance, streamingFilter bool)
	SendLocalReply(response_code int, body_text string, headers map[string]string, details string)
}

type EncoderFilterCallbacks interface {
	StreamFilterCallbacks
	ContinueEncoding()
	AddEncodedData(buffer BufferInstance, streamingFilter bool)
	SendLocalReply(response_code int, body_text string, headers map[string]string, details string)
}

type FilterCallbackHandler interface {
	DecoderFilterCallbacks
	EncoderFilterCallbacks
}
