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

package network

import (
	"unsafe"

	"github.com/envoyproxy/envoy/contrib/golang/filters/go/pkg/api"
)

type connectionCallback struct {
	wrapper   unsafe.Pointer
	writeFunc func(envoyFilter unsafe.Pointer, buffers unsafe.Pointer, buffersNum int, endStream int)
	closeFunc func(envoyFilter unsafe.Pointer, closeType int)
	infoFunc  func(envoyFilter unsafe.Pointer, infoType int) string
}

var _ api.ConnectionCallback = (*connectionCallback)(nil)

func (n *connectionCallback) LocalAddr() string {
	return n.infoFunc(n.wrapper, int(api.ConnectionInfoLocalAddr))
}

func (n *connectionCallback) RemoteAddr() string {
	return n.infoFunc(n.wrapper, int(api.ConnectionInfoRemoteAddr))
}

func (n *connectionCallback) Write(buffer []byte, endStream bool) {
	var endStreamInt int
	if endStream {
		endStreamInt = 1
	}
	n.writeFunc(n.wrapper, unsafe.Pointer(&buffer[0]), len(buffer), endStreamInt)
}

func (n *connectionCallback) Close(closeType api.ConnectionCloseType) {
	n.closeFunc(n.wrapper, int(closeType))
}
