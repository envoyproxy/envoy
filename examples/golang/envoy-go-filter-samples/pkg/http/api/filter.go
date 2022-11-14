package api

// request
type HttpDecoderFilter interface {
	DecodeHeaders(RequestHeaderMap, bool) StatusType
	DecodeData(BufferInstance, bool) StatusType
	DecodeTrailers(RequestTrailerMap) StatusType
	/*
		DecodeMetadata(MetadataMap) StatusType
	*/
}

type HttpFilterConfigFactory func(config interface{}) HttpFilterFactory
type HttpFilterFactory func(callbacks FilterCallbackHandler) HttpFilter

type HttpFilter interface {
	// http request
	HttpDecoderFilter
	// response stream
	StreamEncoderFilter
	/*
		// stream complete
		OnStreamComplete()
		// error log
		Log(LogType, string)
	*/
	// destroy filter
	OnDestroy(DestroyReason)
	Callbacks() FilterCallbacks
}

// response
type StreamEncoderFilter interface {
	EncodeHeaders(ResponseHeaderMap, bool) StatusType
	EncodeData(BufferInstance, bool) StatusType
	EncodeTrailers(ResponseTrailerMap) StatusType
	/*
		EncodeMetadata(MetadataMap) StatusType
		EncoderCallbacks() EncoderFilterCallbacks
	*/
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

type FilterCallbacks interface {
	// Continue or SendLocalReply should be last API invoked, no more code after them.
	Continue(StatusType)
	SendLocalReply(response_code int, body_text string, headers map[string]string, grpc_status int64, details string)
	/*
		AddDecodedData(buffer BufferInstance, streamingFilter bool)
	*/
}

type FilterCallbackHandler interface {
	FilterCallbacks
}
