package main

import (
	"fmt"

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
	default:
		panic(fmt.Sprintf("unknown case %s", query))
	}
	return api.Continue
}
