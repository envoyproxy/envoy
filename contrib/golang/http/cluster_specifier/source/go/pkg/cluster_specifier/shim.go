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

#cgo CFLAGS: -I../api
#cgo linux LDFLAGS: -Wl,-unresolved-symbols=ignore-all
#cgo darwin LDFLAGS: -Wl,-undefined,dynamic_lookup

#include <stdlib.h>
#include <string.h>

#include "api.h"

*/
import "C"

import (
	"github.com/envoyproxy/envoy/contrib/golang/http/common/go/utils"
)

//export envoyGoOnClusterSpecify
func envoyGoOnClusterSpecify(headerPtr uint64, configId uint64, bufferPtr uint64, bufferLen uint64) int64 {
	specifier := getClusterSpecifier(configId)
	cluster := specifier.Choose()
	l := uint64(len(cluster))
	if l == 0 {
		// means use the default cluster
		return 0
	}
	if l > bufferLen {
		// buffer length is not large enough.
		return int64(l)
	}
	buffer := utils.BufferToSlice(bufferPtr, bufferLen)
	copy(buffer, cluster)
	return int64(l)
}
