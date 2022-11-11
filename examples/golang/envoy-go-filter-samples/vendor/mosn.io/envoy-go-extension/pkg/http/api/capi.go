package api

import "unsafe"

type HttpCAPI interface {
	HttpContinue(r unsafe.Pointer, status uint64)
	HttpSendLocalReply(r unsafe.Pointer, response_code int, body_text string, headers map[string]string, grpc_status int64, details string)

	// experience api, memory unsafe
	HttpGetHeader(r unsafe.Pointer, key *string, value *string)
	HttpCopyHeaders(r unsafe.Pointer, num uint64, bytes uint64) map[string]string
	HttpSetHeader(r unsafe.Pointer, key *string, value *string)
	HttpRemoveHeader(r unsafe.Pointer, key *string)

	HttpGetBuffer(r unsafe.Pointer, bufferPtr uint64, value *string, length uint64)
	HttpSetBuffer(r unsafe.Pointer, bufferPtr uint64, value string)

	HttpCopyTrailers(r unsafe.Pointer, num uint64, bytes uint64) map[string]string
	HttpSetTrailer(r unsafe.Pointer, key *string, value *string)

	HttpFinalize(r unsafe.Pointer, reason int)
}
