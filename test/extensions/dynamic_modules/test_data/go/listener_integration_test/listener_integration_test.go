package main

import (
	sdk "github.com/envoyproxy/envoy/source/extensions/dynamic_modules/sdk/go"
	_ "github.com/envoyproxy/envoy/source/extensions/dynamic_modules/sdk/go/abi"
	"github.com/envoyproxy/envoy/source/extensions/dynamic_modules/sdk/go/shared"
)

func init() {
	sdk.RegisterListenerFilterConfigFactories(map[string]shared.ListenerFilterConfigFactory{
		"write_to_socket":  &writeToSocketConfigFactory{},
		"buffer_read":      &bufferReadConfigFactory{},
		"dynamic_metadata": &dynamicMetadataConfigFactory{},
	})
}

func main() {}

type writeToSocketConfigFactory struct {
	shared.EmptyListenerFilterConfigFactory
}

func (f *writeToSocketConfigFactory) Create(shared.ListenerFilterConfigHandle, []byte) (shared.ListenerFilterFactory, error) {
	return &writeToSocketFactory{}, nil
}

type writeToSocketFactory struct {
	shared.EmptyListenerFilterFactory
}

func (f *writeToSocketFactory) Create(handle shared.ListenerFilterHandle) shared.ListenerFilter {
	return &writeToSocketFilter{handle: handle}
}

type writeToSocketFilter struct {
	handle shared.ListenerFilterHandle
	shared.EmptyListenerFilter
}

func (f *writeToSocketFilter) OnAccept() shared.ListenerFilterStatus {
	if f.handle.GetConnectionStartTimeMs() == 0 {
		panic("expected connection start time")
	}
	if _, _, ok := f.handle.GetRemoteAddress(); !ok {
		panic("expected remote address")
	}
	if _, _, ok := f.handle.GetLocalAddress(); !ok {
		panic("expected local address")
	}
	f.handle.SetRequestedServerName("sdk.listener.test")
	f.handle.SetDetectedTransportProtocol("sdk_listener")
	return shared.ListenerFilterStatusContinue
}

type bufferReadConfigFactory struct {
	shared.EmptyListenerFilterConfigFactory
}

func (f *bufferReadConfigFactory) Create(shared.ListenerFilterConfigHandle, []byte) (shared.ListenerFilterFactory, error) {
	return &bufferReadFactory{}, nil
}

type bufferReadFactory struct {
	shared.EmptyListenerFilterFactory
}

func (f *bufferReadFactory) Create(handle shared.ListenerFilterHandle) shared.ListenerFilter {
	return &bufferReadFilter{handle: handle}
}

type bufferReadFilter struct {
	handle shared.ListenerFilterHandle
	shared.EmptyListenerFilter
}

func (f *bufferReadFilter) OnAccept() shared.ListenerFilterStatus {
	return shared.ListenerFilterStatusStop
}

func (f *bufferReadFilter) OnData(dataLength uint64) shared.ListenerFilterStatus {
	if dataLength < 4 {
		panic("expected at least 4 bytes")
	}
	chunk, ok := f.handle.GetBufferChunk()
	if !ok || chunk.ToString()[:4] != "ping" {
		panic("unexpected buffer contents")
	}
	if f.handle.GetCurrentMaxReadBytes() != 4 {
		panic("unexpected max read bytes")
	}
	return shared.ListenerFilterStatusContinue
}

func (f *bufferReadFilter) MaxReadBytes() uint64 { return 4 }

type dynamicMetadataConfigFactory struct {
	shared.EmptyListenerFilterConfigFactory
}

func (f *dynamicMetadataConfigFactory) Create(shared.ListenerFilterConfigHandle, []byte) (shared.ListenerFilterFactory, error) {
	return &dynamicMetadataFactory{}, nil
}

type dynamicMetadataFactory struct {
	shared.EmptyListenerFilterFactory
}

func (f *dynamicMetadataFactory) Create(handle shared.ListenerFilterHandle) shared.ListenerFilter {
	return &dynamicMetadataFilter{handle: handle}
}

type dynamicMetadataFilter struct {
	handle shared.ListenerFilterHandle
	shared.EmptyListenerFilter
}

func (f *dynamicMetadataFilter) OnAccept() shared.ListenerFilterStatus {
	// A serialized google.protobuf.Struct with "k" set to "v".
	if !f.handle.SetDynamicMetadataStruct("dynamic_modules.struct",
		[]byte{0x0a, 0x08, 0x0a, 0x01, 0x6b, 0x12, 0x03, 0x1a, 0x01, 0x76}) {
		panic("expected the struct to be merged")
	}
	if value, ok := f.handle.GetDynamicMetadataString("dynamic_modules.struct", "k"); !ok || value.ToUnsafeString() != "v" {
		panic("expected struct metadata")
	}

	// A serialized google.protobuf.Any with type URL "t/x" and value 0x01 0x02.
	if !f.handle.SetDynamicTypedMetadata("dynamic_modules.typed",
		[]byte{0x0a, 0x03, 0x74, 0x2f, 0x78, 0x12, 0x02, 0x01, 0x02}) {
		panic("expected the typed metadata to be set")
	}

	// A struct that does not parse and an Any without a type URL are rejected.
	if f.handle.SetDynamicMetadataStruct("dynamic_modules.struct", []byte{0x0f}) ||
		f.handle.SetDynamicTypedMetadata("dynamic_modules.typed", nil) {
		panic("expected the invalid buffers to be rejected")
	}
	return shared.ListenerFilterStatusContinue
}
