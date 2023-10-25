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
	"sync"
	"unsafe"

	"github.com/envoyproxy/envoy/contrib/golang/common/go/api"
)

type connectionCallback struct {
	wrapper        unsafe.Pointer
	writeFunc      func(envoyFilter unsafe.Pointer, buffers unsafe.Pointer, buffersNum int, endStream int)
	closeFunc      func(envoyFilter unsafe.Pointer, closeType int)
	infoFunc       func(envoyFilter unsafe.Pointer, infoType int) string
	streamInfo     api.StreamInfo
	state          *filterState
	sema           sync.WaitGroup
	waitingOnEnvoy int32
	mutex          sync.Mutex
}

var _ api.ConnectionCallback = (*connectionCallback)(nil)
var _ api.StreamInfo = (*connectionCallback)(nil)

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

func (n *connectionCallback) StreamInfo() api.StreamInfo {
	return n
}

func (n *connectionCallback) GetRouteName() string {
	panic("implement me")
}

func (n *connectionCallback) FilterChainName() string {
	panic("implement me")
}

func (n *connectionCallback) Protocol() (string, bool) {
	panic("implement me")
}

func (n *connectionCallback) ResponseCode() (uint32, bool) {
	panic("implement me")
}

func (n *connectionCallback) ResponseCodeDetails() (string, bool) {
	panic("implement me")
}

func (n *connectionCallback) AttemptCount() uint32 {
	panic("implement me")
}

func (n *connectionCallback) DynamicMetadata() api.DynamicMetadata {
	panic("implement me")
}

func (n *connectionCallback) DownstreamLocalAddress() string {
	panic("implement me")
}

func (n *connectionCallback) DownstreamRemoteAddress() string {
	panic("implement me")
}

// UpstreamLocalAddress return the upstream local address.
func (n *connectionCallback) UpstreamLocalAddress() (string, bool) {
	return n.infoFunc(n.wrapper, int(api.ConnectionInfoLocalAddr)), true
}

// UpstreamRemoteAddress return the upstream remote address.
func (n *connectionCallback) UpstreamRemoteAddress() (string, bool) {
	return n.infoFunc(n.wrapper, int(api.ConnectionInfoRemoteAddr)), true
}

func (n *connectionCallback) UpstreamClusterName() (string, bool) {
	panic("implement me")
}

func (n *connectionCallback) FilterState() api.FilterState {
	return n.state
}

func (n *connectionCallback) VirtualClusterName() (string, bool) {
	panic("implement me")
}

type filterState struct {
	wrapper unsafe.Pointer
	setFunc func(envoyFilter unsafe.Pointer, key string, value string, stateType api.StateType, lifeSpan api.LifeSpan, streamSharing api.StreamSharing)
	getFunc func(envoyFilter unsafe.Pointer, key string) string
}

var _ api.FilterState = (*filterState)(nil)

func (f *filterState) SetString(key, value string, stateType api.StateType, lifeSpan api.LifeSpan, streamSharing api.StreamSharing) {
	if f.setFunc != nil {
		f.setFunc(unsafe.Pointer(f), key, value, stateType, lifeSpan, streamSharing)
	}
}

func (f *filterState) GetString(key string) string {
	if f.getFunc != nil {
		return f.getFunc(unsafe.Pointer(f), key)
	}
	return ""
}
