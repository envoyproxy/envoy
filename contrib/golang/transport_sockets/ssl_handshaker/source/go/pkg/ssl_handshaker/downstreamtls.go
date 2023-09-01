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

package ssl_handshaker

import (
	"reflect"
	"runtime"
	"unsafe"

	"github.com/envoyproxy/envoy/contrib/golang/common/go/api"
	"github.com/envoyproxy/envoy/contrib/golang/common/go/utils"
)

// DownstreamTlsConn the downstream TLS connection
// TODO: add methods to get TLS info on demand
type DownstreamTlsConn struct {
	envoyTlsHandshaker uint64
}

// DownstreamTlsCertSelectorFactory function for creating DownstreamTlsCertSelector
type DownstreamTlsCertSelectorFactory func(conn *DownstreamTlsConn) api.DownstreamTlsCertSelector

var downstreamTlsCertSelectorFactory DownstreamTlsCertSelectorFactory = defaultDownstreamTlsCertSelector

func RegisterDownstreamTlsCertSelectorFactory(factory DownstreamTlsCertSelectorFactory) {
	if factory != nil {
		downstreamTlsCertSelectorFactory = factory
	}
}

// defaultNoCert no cert provided by default.
type defaultNoCert struct {
}

func (*defaultNoCert) TrySelectCertSync(serverName string) (certName string, done bool) {
	return "", true
}

func (*defaultNoCert) SelectCertAsync(serverName string) string {
	return ""
}

func defaultDownstreamTlsCertSelector(conn *DownstreamTlsConn) api.DownstreamTlsCertSelector {
	return &defaultNoCert{}
}

func NewDownstreamTlsConn(envoyTlsHandshaker uint64) *DownstreamTlsConn {
	conn := &DownstreamTlsConn{
		envoyTlsHandshaker: envoyTlsHandshaker,
	}
	runtime.SetFinalizer(conn, downstreamTlsConnFinalizer)

	return conn
}

const (
	TlsHandshakerRespSync  = 0 // still in the same Envoy work thread
	TlsHandshakerRespAsync = 1 // may in go thread
)

func (c *DownstreamTlsConn) OnSelectCert(serverName string) {
	selector := downstreamTlsCertSelectorFactory(c)
	certName, done := selector.TrySelectCertSync(serverName)
	if done {
		c.selectCert(certName, true)
	} else {
		go func() {
			copyName := utils.CopyString(serverName)
			certName := selector.SelectCertAsync(copyName)
			c.selectCert(certName, false)
		}()
	}
}

func (c *DownstreamTlsConn) selectCert(name string, sync bool) {
	// DownstreamTlsConnSelectCert already contains finalizer in the Envoy side
	runtime.SetFinalizer(c, nil)

	stringHeader := (*reflect.StringHeader)(unsafe.Pointer(&name))
	respType := TlsHandshakerRespSync
	if !sync {
		respType = TlsHandshakerRespAsync
	}

	cAPI.DownstreamTlsConnSelectCert(c.envoyTlsHandshaker, respType, uint64(stringHeader.Data), len(name))
}

func downstreamTlsConnFinalizer(c *DownstreamTlsConn) {
	// no selectCert invoked before, it's finalizing by GC.
	// use an empty name, envoy will close the connection.
	c.selectCert("", false)
}
