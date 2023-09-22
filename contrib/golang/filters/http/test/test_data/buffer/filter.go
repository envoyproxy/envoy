package main

import (
	"fmt"
	"reflect"

	"github.com/envoyproxy/envoy/contrib/golang/common/go/api"
)

type filter struct {
	api.PassThroughStreamFilter

	callbacks api.FilterCallbackHandler
	path      string
	config    *config

	failed bool
}

func testReset(b api.BufferInstance) {
	b.Reset()

	bs := b.Bytes()
	if len(bs) > 0 {
		panic(fmt.Sprintf("unexpected data: %s", string(bs)))
	}
}

func testDrain(b api.BufferInstance) {
	b.Drain(40)
	bs := b.Bytes()
	if string(bs) != "1234512345" {
		panic(fmt.Sprintf("unexpected data: %s", string(bs)))
	}

	b.Drain(5)
	bs = b.Bytes()
	if string(bs) != "12345" {
		panic(fmt.Sprintf("unexpected data: %s", string(bs)))
	}

	b.Drain(10)
	bs = b.Bytes()
	if string(bs) != "" {
		panic(fmt.Sprintf("unexpected data: %s", string(bs)))
	}

	// drain when all data are drained
	b.Drain(10)
	bs = b.Bytes()
	if string(bs) != "" {
		panic(fmt.Sprintf("unexpected data: %s", string(bs)))
	}

	// bad offset
	for _, n := range []int{-1, 0} {
		b.Drain(n)
	}
}

func testResetAfterDrain(b api.BufferInstance) {
	b.Drain(40)
	b.Reset()
	bs := b.Bytes()
	if string(bs) != "" {
		panic(fmt.Sprintf("unexpected data: %s", string(bs)))
	}
}

func panicIfNotEqual(a, b any) {
	if !reflect.DeepEqual(a, b) {
		panic(fmt.Sprintf("expected %v, got %v", a, b))
	}
}

func panicIfLenMismatch(b api.BufferInstance, size int) {
	panicIfNotEqual(size, b.Len())
	panicIfNotEqual(len(b.Bytes()), b.Len())
}

func testLen(b api.BufferInstance) {
	b.Set([]byte("12"))
	panicIfLenMismatch(b, 2)
	b.SetString("123")
	panicIfLenMismatch(b, 3)

	b.Write([]byte("45"))
	panicIfLenMismatch(b, 5)
	b.WriteString("67")
	panicIfLenMismatch(b, 7)
	b.WriteByte('8')
	panicIfLenMismatch(b, 8)
	b.WriteUint16(90)
	panicIfLenMismatch(b, 10)
	b.WriteUint32(12)
	panicIfLenMismatch(b, 12)
	b.WriteUint64(12)
	panicIfLenMismatch(b, 14)

	b.Drain(2)
	panicIfLenMismatch(b, 12)
	b.Write([]byte("45"))
	panicIfLenMismatch(b, 14)

	b.Reset()
	panicIfLenMismatch(b, 0)

	b.Append([]byte("12"))
	panicIfLenMismatch(b, 2)
	b.Prepend([]byte("0"))
	panicIfLenMismatch(b, 3)
	b.AppendString("345")
	panicIfLenMismatch(b, 6)
	b.PrependString("00")
	panicIfLenMismatch(b, 8)
}

func (f *filter) DecodeData(buffer api.BufferInstance, endStream bool) api.StatusType {
	if endStream {
		return api.Continue
	}
	// run once

	query, _ := f.callbacks.GetProperty("request.query")
	switch query {
	case "Reset":
		testReset(buffer)
	case "ResetAfterDrain":
		testResetAfterDrain(buffer)
	case "Drain":
		testDrain(buffer)
	case "Len":
		testLen(buffer)
	default:
		panic(fmt.Sprintf("unknown case %s", query))
	}
	return api.Continue
}
