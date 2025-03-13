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

import (
	"fmt"
)

func (c *commonCApiImpl) Log(level LogType, message string) {
	panic("To implement")
}

func (c *commonCApiImpl) LogLevel() LogType {
	panic("To implement")
}

func LogTrace(message string) {
	if cAPI.LogLevel() > Trace {
		return
	}
	cAPI.Log(Trace, message)
}

func LogDebug(message string) {
	if cAPI.LogLevel() > Debug {
		return
	}
	cAPI.Log(Debug, message)
}

func LogInfo(message string) {
	if cAPI.LogLevel() > Info {
		return
	}
	cAPI.Log(Info, message)
}

func LogWarn(message string) {
	if cAPI.LogLevel() > Warn {
		return
	}
	cAPI.Log(Warn, message)
}

func LogError(message string) {
	if cAPI.LogLevel() > Error {
		return
	}
	cAPI.Log(Error, message)
}

func LogCritical(message string) {
	if cAPI.LogLevel() > Critical {
		return
	}
	cAPI.Log(Critical, message)
}

func LogTracef(format string, v ...any) {
	if cAPI.LogLevel() > Trace {
		return
	}
	cAPI.Log(Trace, fmt.Sprintf(format, v...))
}

func LogDebugf(format string, v ...any) {
	if cAPI.LogLevel() > Debug {
		return
	}
	cAPI.Log(Debug, fmt.Sprintf(format, v...))
}

func LogInfof(format string, v ...any) {
	if cAPI.LogLevel() > Info {
		return
	}
	cAPI.Log(Info, fmt.Sprintf(format, v...))
}

func LogWarnf(format string, v ...any) {
	if cAPI.LogLevel() > Warn {
		return
	}
	cAPI.Log(Warn, fmt.Sprintf(format, v...))
}

func LogErrorf(format string, v ...any) {
	if cAPI.LogLevel() > Error {
		return
	}
	cAPI.Log(Error, fmt.Sprintf(format, v...))
}

func LogCriticalf(format string, v ...any) {
	if cAPI.LogLevel() > Critical {
		return
	}
	cAPI.Log(Critical, fmt.Sprintf(format, v...))
}

func GetLogLevel() LogType {
	return cAPI.LogLevel()
}
