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

package tcp

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

	"github.com/envoyproxy/envoy/contrib/golang/common/go/api"
	"github.com/envoyproxy/envoy/contrib/golang/common/go/utils"
)

var (
	configNumGenerator uint64
	configCache        = &sync.Map{} // uint64 -> config(interface{})
)

//export envoyGoHttpTcpBridgeOnConfig
func envoyGoHttpTcpBridgeOnConfig(c *C.httpConfig) uint64 {
	buf := utils.BytesToSlice(uint64(c.config_ptr), uint64(c.config_len))
	var any anypb.Any
	proto.Unmarshal(buf, &any)

	configNum := atomic.AddUint64(&configNumGenerator, 1)

	name := utils.BytesToString(uint64(c.plugin_name_ptr), uint64(c.plugin_name_len))
	configParser := getHttpTcpBridgeConfigParser(name)

	var parsedConfig interface{}
	var err error
	parsedConfig, err = configParser.Parse(&any)

	if err != nil {
		api.LogErrorf("go side: golang http-tcp bridge: failed to parse golang plugin config: %v", err)
		return 0
	}
	configCache.Store(configNum, parsedConfig)

	return configNum
}

//export envoyGoHttpTcpBridgeDestroyPluginConfig
func envoyGoHttpTcpBridgeDestroyPluginConfig(id uint64) {
	configCache.Delete(id)
}
