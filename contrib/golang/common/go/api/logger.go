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
import (
	"fmt"
	"unsafe"
)

// The default log format is:
// [2023-08-09 03:04:15.985][1390][critical][golang] [contrib/golang/common/log/cgo.cc:27] msg

func (c *commonCApiImpl) Log(level LogType, message string) {
	C.envoyGoFilterLog(C.uint32_t(level), unsafe.Pointer(&message), C.int(len(message)))
}

func (c *commonCApiImpl) LogLevel() LogType {
	return LogType(C.envoyGoFilterLogLevel())
}

func LogTrace(message string) {
	cAPI.Log(Trace, message)
}

func LogDebug(message string) {
	cAPI.Log(Debug, message)
}

func LogInfo(message string) {
	cAPI.Log(Info, message)
}

func LogWarn(message string) {
	cAPI.Log(Warn, message)
}

func LogError(message string) {
	cAPI.Log(Error, message)
}

func LogCritical(message string) {
	cAPI.Log(Critical, message)
}

func LogTracef(format string, v ...any) {
	LogTrace(fmt.Sprintf(format, v...))
}

func LogDebugf(format string, v ...any) {
	LogDebug(fmt.Sprintf(format, v...))
}

func LogInfof(format string, v ...any) {
	LogInfo(fmt.Sprintf(format, v...))
}

func LogWarnf(format string, v ...any) {
	LogWarn(fmt.Sprintf(format, v...))
}

func LogErrorf(format string, v ...any) {
	LogError(fmt.Sprintf(format, v...))
}

func LogCriticalf(format string, v ...any) {
	LogCritical(fmt.Sprintf(format, v...))
}

func GetLogLevel() LogType {
	return cAPI.LogLevel()
}
