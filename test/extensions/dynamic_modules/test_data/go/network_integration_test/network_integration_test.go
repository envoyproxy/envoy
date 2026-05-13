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

func (f *connectionStateConfigFactory) Create(shared.NetworkFilterConfigHandle, []byte) (shared.NetworkFilterFactory, error) {
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
