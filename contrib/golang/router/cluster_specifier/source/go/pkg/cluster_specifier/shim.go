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

import "C"
import (
	"fmt"

	"github.com/envoyproxy/envoy/contrib/golang/common/go/utils"
)

const errPanic = -1

//export envoyGoOnClusterSpecify
func envoyGoOnClusterSpecify(pluginPtr uint64, headerPtr uint64, pluginId uint64, bufferPtr uint64, bufferLen uint64) (l int64) {
	defer func() {
		if err := recover(); err != nil {
			msg := fmt.Sprintf("golang cluster specifier plugin panic: %v", err)
			cAPI.HttpLogError(pluginPtr, &msg)
			l = errPanic
		}
	}()
	header := &httpHeaderMap{
		headerPtr: headerPtr,
	}
	specifier := getClusterSpecifier(pluginId)
	if specifier == nil {
		panic(fmt.Sprintf("no registered cluster specifier plugin for id: %d", pluginId))
	}
	cluster := specifier.Cluster(header)
	clusterLen := uint64(len(cluster))
	if clusterLen == 0 {
		// means use the default cluster
		return 0
	}
	if clusterLen > bufferLen {
		// buffer length is not large enough.
		return int64(clusterLen)
	}
	buffer := utils.BufferToSlice(bufferPtr, clusterLen)
	copy(buffer, cluster)
	return int64(clusterLen)
}
