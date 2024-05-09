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

package api_impl

/*
// ref https://github.com/golang/go/issues/25832

#cgo CFLAGS: -I../api
#cgo linux LDFLAGS: -Wl,-unresolved-symbols=ignore-all
#cgo darwin LDFLAGS: -Wl,-undefined,dynamic_lookup

#include <stdlib.h>
#include <string.h>

#include "api.h"

*/
import "C"
import (
	"os"
	"sync/atomic"
	"time"
	"unsafe"

	"github.com/envoyproxy/envoy/contrib/golang/common/go/api"
)

var (
	currLogLevel atomic.Int32
)

type commonCApiImpl struct{}

// The default log format is:
// [2023-08-09 03:04:15.985][1390][critical][golang] [contrib/golang/common/log/cgo.cc:27] msg

func (c *commonCApiImpl) Log(level api.LogType, message string) {
	C.envoyGoFilterLog(C.uint32_t(level), unsafe.Pointer(unsafe.StringData(message)), C.int(len(message)))
}

func (c *commonCApiImpl) LogLevel() api.LogType {
	lv := currLogLevel.Load()
	return api.LogType(lv)
}

func init() {
	api.SetCommonCAPI(&commonCApiImpl{})

	interval := time.Second
	envInterval := os.Getenv("ENVOY_GOLANG_LOG_LEVEL_SYNC_INTERVAL")
	if envInterval != "" {
		dur, err := time.ParseDuration(envInterval)
		if err == nil && dur >= time.Millisecond {
			// protect against too frequent sync
			interval = dur
		} else {
			api.LogErrorf("invalid env var ENVOY_GOLANG_LOG_LEVEL_SYNC_INTERVAL: %s", envInterval)
		}
	}

	currLogLevel.Store(int32(C.envoyGoFilterLogLevel()))
	ticker := time.NewTicker(interval)
	go func() {
		for {
			select {
			case <-ticker.C:
				currLogLevel.Store(int32(C.envoyGoFilterLogLevel()))
			}
		}
	}()
}
