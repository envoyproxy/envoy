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

package utils

import (
	"testing"
	"unsafe"
)

// goString has the layout of a Go string header (GoString on the C side).
type goString struct {
	p unsafe.Pointer
	n int
}

// lengthsOnly returns strings that carry a length but a nil data pointer,
// written the same way the C++ side fills the Go string array.
func lengthsOnly(lens ...int) []string {
	strs := make([]string, len(lens))
	for i, n := range lens {
		(*goString)(unsafe.Pointer(&strs[i])).n = n
	}
	return strs
}

func TestBuildStringsFromBuffer(t *testing.T) {
	buf := []byte("content-typetext/plainx-emptyx-idabc")
	strs := lengthsOnly(12, 10, 7, 0, 4, 3)
	BuildStringsFromBuffer(strs, buf)

	want := []string{"content-type", "text/plain", "x-empty", "", "x-id", "abc"}
	for i := range want {
		if strs[i] != want[i] {
			t.Fatalf("strs[%d] = %q, want %q", i, strs[i], want[i])
		}
	}
	// The strings share buf; nothing is copied.
	if unsafe.StringData(strs[0]) != &buf[0] || unsafe.StringData(strs[5]) != &buf[len(buf)-3] {
		t.Fatal("strings are not backed by buf")
	}
}

func TestBuildStringsFromBufferUnusedEntries(t *testing.T) {
	// Callers may pass a larger, zeroed array; unused entries stay empty.
	strs := append(lengthsOnly(1, 1), make([]string, 4)...)
	BuildStringsFromBuffer(strs, []byte("kv"))
	if strs[0] != "k" || strs[1] != "v" {
		t.Fatalf("got %q %q", strs[0], strs[1])
	}
	for _, s := range strs[2:] {
		if s != "" {
			t.Fatalf("unused entry = %q, want empty", s)
		}
	}
}

func TestBuildStringsFromBufferShortBufferPanics(t *testing.T) {
	defer func() {
		if recover() == nil {
			t.Fatal("expected a bounds panic when the lengths exceed buf")
		}
	}()
	BuildStringsFromBuffer(lengthsOnly(3), []byte("ab"))
}
