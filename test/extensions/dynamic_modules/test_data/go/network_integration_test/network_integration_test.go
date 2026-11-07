package main

import (
	sdk "github.com/envoyproxy/envoy/source/extensions/dynamic_modules/sdk/go"
	_ "github.com/envoyproxy/envoy/source/extensions/dynamic_modules/sdk/go/abi"
	"github.com/envoyproxy/envoy/source/extensions/dynamic_modules/sdk/go/shared"
)

func init() {
	sdk.RegisterNetworkFilterConfigFactories(map[string]shared.NetworkFilterConfigFactory{
		"flow_control":     &flowControlConfigFactory{},
		"connection_state": &connectionStateConfigFactory{},
		"half_close":       &halfCloseConfigFactory{},
		"buffer_limits":    &bufferLimitsConfigFactory{},
		"dynamic_metadata": &dynamicMetadataConfigFactory{},
	})
}

func main() {}

type flowControlConfigFactory struct {
	shared.EmptyNetworkFilterConfigFactory
}

func (f *flowControlConfigFactory) Create(shared.NetworkFilterConfigHandle, []byte) (shared.NetworkFilterFactory, error) {
	return &flowControlFactory{}, nil
}

type flowControlFactory struct {
	shared.EmptyNetworkFilterFactory
}

func (f *flowControlFactory) Create(handle shared.NetworkFilterHandle) shared.NetworkFilter {
	return &flowControlFilter{handle: handle}
}

type flowControlFilter struct {
	handle        shared.NetworkFilterHandle
	readsDisabled bool
	shared.EmptyNetworkFilter
}

func (f *flowControlFilter) OnRead(shared.NetworkBuffer, bool) shared.NetworkFilterStatus {
	if !f.readsDisabled {
		if status := f.handle.ReadDisable(true); status != shared.NetworkReadDisableStatusTransitionedToReadDisabled {
			panic("unexpected disable status")
		}
		if f.handle.ReadEnabled() {
			panic("expected reads to be disabled")
		}
		f.readsDisabled = true

		if status := f.handle.ReadDisable(false); status != shared.NetworkReadDisableStatusTransitionedToReadEnabled {
			panic("unexpected enable status")
		}
		if !f.handle.ReadEnabled() {
			panic("expected reads to be enabled")
		}
	}
	return shared.NetworkFilterStatusContinue
}

type connectionStateConfigFactory struct {
	shared.EmptyNetworkFilterConfigFactory
}

func (f *connectionStateConfigFactory) Create(handle shared.NetworkFilterConfigHandle, _ []byte) (shared.NetworkFilterFactory, error) {
	// Emit a metric directly from the config context (no per-connection filter), exercising the
	// config-scoped emission path. This would typically be done from a scheduled background task.
	configTotal, _ := handle.DefineCounter("config_total")
	handle.IncrementCounterValue(configTotal, 1)
	return &connectionStateFactory{}, nil
}

type connectionStateFactory struct {
	shared.EmptyNetworkFilterFactory
}

func (f *connectionStateFactory) Create(handle shared.NetworkFilterHandle) shared.NetworkFilter {
	return &connectionStateFilter{handle: handle}
}

type connectionStateFilter struct {
	handle shared.NetworkFilterHandle
	shared.EmptyNetworkFilter
}

func (f *connectionStateFilter) OnNewConnection() shared.NetworkFilterStatus {
	if f.handle.GetConnectionState() != shared.NetworkConnectionStateOpen {
		panic("expected open state on new connection")
	}
	return shared.NetworkFilterStatusContinue
}

func (f *connectionStateFilter) OnRead(shared.NetworkBuffer, bool) shared.NetworkFilterStatus {
	if f.handle.GetConnectionState() != shared.NetworkConnectionStateOpen {
		panic("expected open state on read")
	}
	return shared.NetworkFilterStatusContinue
}

func (f *connectionStateFilter) OnWrite(shared.NetworkBuffer, bool) shared.NetworkFilterStatus {
	if f.handle.GetConnectionState() != shared.NetworkConnectionStateOpen {
		panic("expected open state on write")
	}
	return shared.NetworkFilterStatusContinue
}

type halfCloseConfigFactory struct {
	shared.EmptyNetworkFilterConfigFactory
}

func (f *halfCloseConfigFactory) Create(shared.NetworkFilterConfigHandle, []byte) (shared.NetworkFilterFactory, error) {
	return &halfCloseFactory{}, nil
}

type halfCloseFactory struct {
	shared.EmptyNetworkFilterFactory
}

func (f *halfCloseFactory) Create(handle shared.NetworkFilterHandle) shared.NetworkFilter {
	return &halfCloseFilter{handle: handle}
}

type halfCloseFilter struct {
	handle shared.NetworkFilterHandle
	shared.EmptyNetworkFilter
}

func (f *halfCloseFilter) OnNewConnection() shared.NetworkFilterStatus {
	if !f.handle.IsHalfCloseEnabled() {
		panic("expected half-close to be enabled")
	}
	return shared.NetworkFilterStatusContinue
}

func (f *halfCloseFilter) OnRead(shared.NetworkBuffer, bool) shared.NetworkFilterStatus {
	f.handle.EnableHalfClose(false)
	if f.handle.IsHalfCloseEnabled() {
		panic("expected half-close to be disabled")
	}
	f.handle.EnableHalfClose(true)
	if !f.handle.IsHalfCloseEnabled() {
		panic("expected half-close to be re-enabled")
	}
	return shared.NetworkFilterStatusContinue
}

type bufferLimitsConfigFactory struct {
	shared.EmptyNetworkFilterConfigFactory
}

func (f *bufferLimitsConfigFactory) Create(shared.NetworkFilterConfigHandle, []byte) (shared.NetworkFilterFactory, error) {
	return &bufferLimitsFactory{}, nil
}

type bufferLimitsFactory struct {
	shared.EmptyNetworkFilterFactory
}

func (f *bufferLimitsFactory) Create(handle shared.NetworkFilterHandle) shared.NetworkFilter {
	return &bufferLimitsFilter{handle: handle}
}

type bufferLimitsFilter struct {
	handle shared.NetworkFilterHandle
	shared.EmptyNetworkFilter
}

func (f *bufferLimitsFilter) OnNewConnection() shared.NetworkFilterStatus {
	_ = f.handle.GetBufferLimit()
	f.handle.SetBufferLimits(32768)
	if f.handle.GetBufferLimit() != 32768 {
		panic("expected updated buffer limit")
	}
	return shared.NetworkFilterStatusContinue
}

type dynamicMetadataConfigFactory struct {
	shared.EmptyNetworkFilterConfigFactory
}

func (f *dynamicMetadataConfigFactory) Create(shared.NetworkFilterConfigHandle, []byte) (shared.NetworkFilterFactory, error) {
	return &dynamicMetadataFactory{}, nil
}

type dynamicMetadataFactory struct {
	shared.EmptyNetworkFilterFactory
}

func (f *dynamicMetadataFactory) Create(handle shared.NetworkFilterHandle) shared.NetworkFilter {
	return &dynamicMetadataFilter{handle: handle}
}

type dynamicMetadataFilter struct {
	handle shared.NetworkFilterHandle
	shared.EmptyNetworkFilter
}

func (f *dynamicMetadataFilter) OnNewConnection() shared.NetworkFilterStatus {
	// A serialized google.protobuf.Struct with "k" set to "v".
	if !f.handle.SetMetadataStruct("dynamic_modules.struct",
		[]byte{0x0a, 0x08, 0x0a, 0x01, 0x6b, 0x12, 0x03, 0x1a, 0x01, 0x76}) {
		panic("expected the struct to be merged")
	}
	if value, ok := f.handle.GetMetadataString("dynamic_modules.struct", "k"); !ok || value.ToUnsafeString() != "v" {
		panic("expected struct metadata")
	}

	// A serialized google.protobuf.Any with type URL "t/x" and value 0x01 0x02.
	if !f.handle.SetTypedMetadata("dynamic_modules.typed",
		[]byte{0x0a, 0x03, 0x74, 0x2f, 0x78, 0x12, 0x02, 0x01, 0x02}) {
		panic("expected the typed metadata to be set")
	}

	// A struct that does not parse and an Any without a type URL are rejected.
	if f.handle.SetMetadataStruct("dynamic_modules.struct", []byte{0x0f}) ||
		f.handle.SetTypedMetadata("dynamic_modules.typed", nil) {
		panic("expected the invalid buffers to be rejected")
	}
	return shared.NetworkFilterStatusContinue
}
