package utility

import (
	"unsafe"

	"github.com/envoyproxy/envoy/source/extensions/dynamic_modules/sdk/go/shared"
)

func isSameChunks(bufferedChunk [][]byte, receivedChunk [][]byte) bool {
	if len(bufferedChunk) != len(receivedChunk) {
		return false
	}
	for i := range bufferedChunk {
		if unsafe.SliceData(bufferedChunk[i]) != unsafe.SliceData(receivedChunk[i]) {
			return false
		}
	}
	return true
}

func getBodyContent(bufferedBody, receivedBody shared.BodyBuffer) []byte {
	var bodySize int
	if bufferedBody != nil {
		bodySize += int(bufferedBody.GetSize())
	}
	if receivedBody != nil {
		bodySize += int(receivedBody.GetSize())
	}
	body := make([]byte, 0, bodySize)

	var bufferedChunks [][]byte

	if bufferedBody != nil {
		bufferedChunks = bufferedBody.GetChunks()
		for _, chunk := range bufferedChunks {
			body = append(body, chunk...)
		}
	}
	if receivedBody != nil {
		receivedChunks := receivedBody.GetChunks()
		// Because the the complex buffering logic in Envoy, it's possible that the
		// latest received body are the same as the buffered body.
		// This will happens when previous filter returns StopAndBuffer, and then this
		// filter is called again with the buffered body as received body.
		// TODO(wbpcode): optimize this by add a new ABI to directly check it.
		if !isSameChunks(bufferedChunks, receivedChunks) {
			// Avoid duplicate appending the same chunks
			for _, chunk := range receivedChunks {
				body = append(body, chunk...)
			}
		}
	}

	return body
}

// ReadWholeRequestBody reads the whole request body by combining the buffered body and the
// latest received body.
// This should only be called after we see the end of the request, which means the end_of_stream flag
// is true in the OnRequestBody callback or we are in the OnRequestTrailers callback.
func ReadWholeRequestBody(handle shared.HttpFilterHandle) []byte {
	return getBodyContent(handle.BufferedRequestBody(), handle.ReceivedRequestBody())
}

// ReadWholeResponseBody reads the whole response body by combining the buffered body and the
// latest received body.
// This should only be called after we see the end of the response, which means the end_of_stream flag
// is true in the OnResponseBody callback or we are in the OnResponseTrailers callback.
func ReadWholeResponseBody(handle shared.HttpFilterHandle) []byte {
	return getBodyContent(handle.BufferedResponseBody(), handle.ReceivedResponseBody())
}
