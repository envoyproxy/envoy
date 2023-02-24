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

package cluster_specifier

/*
// ref https://github.com/golang/go/issues/25832

#cgo linux LDFLAGS: -Wl,-unresolved-symbols=ignore-all
#cgo darwin LDFLAGS: -Wl,-undefined,dynamic_lookup

#include <stdlib.h>
#include <string.h>

#include "api.h"

*/
import "C"

import (
	"sync"
	"sync/atomic"

	"google.golang.org/protobuf/proto"
	"google.golang.org/protobuf/types/known/anypb"

	"github.com/envoyproxy/envoy/contrib/golang/common/go/utils"
)

const errCodeInvalidConfig = 0

var (
	pluginNumGenerator uint64
	pluginCache        = sync.Map{}
)

//export envoyGoClusterSpecifierNewPlugin
func envoyGoClusterSpecifierNewPlugin(configPtr uint64, configLen uint64) uint64 {
	if clusterSpecifierConfigFactory == nil {
		panic("no cluster specifier config factory registered")
	}

	buf := utils.BytesToSlice(configPtr, configLen)
	var any anypb.Any
	if err := proto.Unmarshal(buf, &any); err != nil {
		return errCodeInvalidConfig
	}

	plugin := clusterSpecifierConfigFactory(&any)
	pluginNum := atomic.AddUint64(&pluginNumGenerator, 1)

	pluginCache.Store(pluginNum, plugin)

	return pluginNum
}

//export envoyGoClusterSpecifierDestroyPlugin
func envoyGoClusterSpecifierDestroyPlugin(id uint64) {
	pluginCache.Delete(id)
}
