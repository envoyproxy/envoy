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

package http

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
	"fmt"
	"runtime"
	"sync"
	"sync/atomic"
	"time"

	"google.golang.org/protobuf/proto"
	"google.golang.org/protobuf/types/known/anypb"

	"github.com/envoyproxy/envoy/contrib/golang/common/go/api"
	"github.com/envoyproxy/envoy/contrib/golang/common/go/utils"
)

var (
	configNumGenerator uint64
	configCache        = &sync.Map{}            // uint64 -> config(interface{})
	delayDeleteTime    = time.Millisecond * 100 // 100ms
)

func configFinalize(c *httpConfig) {
	c.Finalize()
}

func createConfig(c *C.httpConfig) *httpConfig {
	config := &httpConfig{
		config: c,
	}
	// NP: make sure httpConfig will be deleted.
	runtime.SetFinalizer(config, configFinalize)

	return config
}

//export envoyGoFilterNewHttpPluginConfig
func envoyGoFilterNewHttpPluginConfig(c *C.httpConfig) uint64 {
	if !api.CgoCheckDisabled() {
		cAPI.HttpLog(api.Error, "The Envoy Golang filter requires the `GODEBUG=cgocheck=0` environment variable set.")
		return 0
	}

	buf := utils.BytesToSlice(uint64(c.config_ptr), uint64(c.config_len))
	var any anypb.Any
	proto.Unmarshal(buf, &any)

	configNum := atomic.AddUint64(&configNumGenerator, 1)

	name := utils.BytesToString(uint64(c.plugin_name_ptr), uint64(c.plugin_name_len))
	configParser := getHttpFilterConfigParser(name)
	if configParser != nil {
		var parsedConfig interface{}
		var err error
		if c.is_route_config == 1 {
			parsedConfig, err = configParser.Parse(&any, nil)
		} else {
			http_config := createConfig(c)
			parsedConfig, err = configParser.Parse(&any, http_config)
		}
		if err != nil {
			cAPI.HttpLog(api.Error, fmt.Sprintf("failed to parse golang plugin config: %v", err))
			return 0
		}
		configCache.Store(configNum, parsedConfig)
	} else {
		configCache.Store(configNum, &any)
	}

	return configNum
}

//export envoyGoFilterDestroyHttpPluginConfig
func envoyGoFilterDestroyHttpPluginConfig(id uint64, needDelay int) {
	if needDelay == 1 {
		// there is a concurrency race in the c++ side:
		// 1. when A envoy worker thread is using the cached merged_config_id_ and it will call into Go after some time.
		// 2. while B envoy worker thread may update the merged_config_id_ in getMergedConfigId, that will delete the id.
		// so, we delay deleting the id in the Go side.
		time.AfterFunc(delayDeleteTime, func() {
			configCache.Delete(id)
		})
	} else {
		// there is no race for non-merged config.
		configCache.Delete(id)
	}
}

//export envoyGoFilterMergeHttpPluginConfig
func envoyGoFilterMergeHttpPluginConfig(namePtr, nameLen, parentId, childId uint64) uint64 {
	name := utils.BytesToString(namePtr, nameLen)
	configParser := getHttpFilterConfigParser(name)

	if configParser != nil {
		parent, ok := configCache.Load(parentId)
		if !ok {
			panic(fmt.Sprintf("merge config: get parentId: %d config failed", parentId))
		}
		child, ok := configCache.Load(childId)
		if !ok {
			panic(fmt.Sprintf("merge config: get childId: %d config failed", childId))
		}

		new := configParser.Merge(parent, child)
		configNum := atomic.AddUint64(&configNumGenerator, 1)
		configCache.Store(configNum, new)
		return configNum

	} else {
		// child override parent by default.
		// It's safe to reuse the childId, since the merged config have the same life time with the child config.
		return childId
	}
}
