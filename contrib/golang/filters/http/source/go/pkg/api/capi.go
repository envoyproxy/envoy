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

	// experience api, memory unsafe
	HttpGetHeader(r unsafe.Pointer, key *string, value *string)
	HttpCopyHeaders(r unsafe.Pointer, num uint64, bytes uint64) map[string]string
	HttpSetHeader(r unsafe.Pointer, key *string, value *string)
	HttpRemoveHeader(r unsafe.Pointer, key *string)

	HttpGetBuffer(r unsafe.Pointer, bufferPtr uint64, value *string, length uint64)
	HttpSetBufferHelper(r unsafe.Pointer, bufferPtr uint64, value string, action BufferAction)

	HttpCopyTrailers(r unsafe.Pointer, num uint64, bytes uint64) map[string]string
	HttpSetTrailer(r unsafe.Pointer, key *string, value *string)

	HttpGetRouteName(r unsafe.Pointer) string

	HttpFinalize(r unsafe.Pointer, reason int)
}
