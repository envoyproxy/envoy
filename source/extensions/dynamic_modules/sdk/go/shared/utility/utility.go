package utility

import (
	"github.com/envoyproxy/envoy/source/extensions/dynamic_modules/sdk/go/shared"
)

func getBodyContent(bufferedBody, receivedBody shared.BodyBuffer, isBuffered bool) []byte {
	var bodySize uint64 = bufferedBody.GetSize()
	if !isBuffered {
		bodySize += receivedBody.GetSize()
	}
	body := make([]byte, 0, bodySize)

	for _, chunk := range bufferedBody.GetChunks() {
		body = append(body, chunk.ToUnsafeBytes()...)
	}
	if isBuffered {
		return body
	}
	for _, chunk := range receivedBody.GetChunks() {
		body = append(body, chunk.ToUnsafeBytes()...)
	}
	return body
}

// ReadWholeRequestBody reads the whole request body by combining the buffered body and the
// latest received body.
// This will copy all request body content into a module owned `[]byte`.
//
// This should only be called after we see the end of the request, which means the end_of_stream flag
// is true in the OnRequestBody callback or we are in the OnRequestTrailers callback.
func ReadWholeRequestBody(handle shared.HttpFilterHandle) []byte {
	return getBodyContent(handle.BufferedRequestBody(), handle.ReceivedRequestBody(),
		handle.ReceivedBufferedRequestBody())
}

// ReadWholeResponseBody reads the whole response body by combining the buffered body and the
// latest received body.
// This will copy all response body content into a module owned `[]byte`.
//
// This should only be called after we see the end of the response, which means the end_of_stream flag
// is true in the OnResponseBody callback or we are in the OnResponseTrailers callback.
func ReadWholeResponseBody(handle shared.HttpFilterHandle) []byte {
	return getBodyContent(handle.BufferedResponseBody(), handle.ReceivedResponseBody(),
		handle.ReceivedBufferedResponseBody())
}
