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

package api

/*
// ref https://github.com/golang/go/issues/25832

#cgo linux LDFLAGS: -Wl,-unresolved-symbols=ignore-all
#cgo darwin LDFLAGS: -Wl,-undefined,dynamic_lookup

#include <stdlib.h>
#include <string.h>

#include "api.h"

*/
import "C"
import "unsafe"

func LogTrace(message string) {
	C.envoyGoFilterLog(C.uint32_t(Trace), unsafe.Pointer(&message))
}

func LogDebug(message string) {
	C.envoyGoFilterLog(C.uint32_t(Debug), unsafe.Pointer(&message))
}

func LogInfo(message string) {
	C.envoyGoFilterLog(C.uint32_t(Info), unsafe.Pointer(&message))
}

func LogWarn(message string) {
	C.envoyGoFilterLog(C.uint32_t(Warn), unsafe.Pointer(&message))
}

func LogError(message string) {
	C.envoyGoFilterLog(C.uint32_t(Error), unsafe.Pointer(&message))
}

func LogCritical(message string) {
	C.envoyGoFilterLog(C.uint32_t(Critical), unsafe.Pointer(&message))
}
