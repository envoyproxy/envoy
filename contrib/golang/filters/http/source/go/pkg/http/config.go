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
    "sync"
    "sync/atomic"

    "google.golang.org/protobuf/proto"
    "google.golang.org/protobuf/types/known/anypb"

    "github.com/envoyproxy/envoy/contrib/golang/filters/http/source/go/pkg/utils"
)

var (
    configNumGenerator uint64
    configCache        = &sync.Map{} // uint64 -> *anypb.Any
)

//export envoyGoFilterNewHttpPluginConfig
func envoyGoFilterNewHttpPluginConfig(configPtr uint64, configLen uint64) uint64 {
    buf := utils.BytesToSlice(configPtr, configLen)
    var any anypb.Any
    proto.Unmarshal(buf, &any)

    configNum := atomic.AddUint64(&configNumGenerator, 1)
    if httpFilterConfigParser != nil {
        configCache.Store(configNum, httpFilterConfigParser.Parse(&any))
    } else {
        configCache.Store(configNum, &any)
    }

    return configNum
}

//export envoyGoFilterDestroyHttpPluginConfig
func envoyGoFilterDestroyHttpPluginConfig(id uint64) {
    configCache.Delete(id)
}

//export envoyGoFilterMergeHttpPluginConfig
func envoyGoFilterMergeHttpPluginConfig(parentId uint64, childId uint64) uint64 {
    if httpFilterConfigParser != nil {
        parent, ok := configCache.Load(parentId)
        if !ok {
            panic(fmt.Sprintf("merge config: get parentId: %d config failed", parentId))
        }
        child, ok := configCache.Load(childId)
        if !ok {
            panic(fmt.Sprintf("merge config: get childId: %d config failed", childId))
        }

        new := httpFilterConfigParser.Merge(parent, child)
        configNum := atomic.AddUint64(&configNumGenerator, 1)
        configCache.Store(configNum, new)
        return configNum

    } else {
        // child override parent by default
        return childId
    }
}
