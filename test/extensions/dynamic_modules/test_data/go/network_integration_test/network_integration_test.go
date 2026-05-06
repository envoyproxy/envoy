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
		"pause_resume":     &pauseResumeConfigFactory{},
		"data_appender":    &dataAppenderConfigFactory{},
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

// =============================================================================
// pause_resume: returns Stop on the first OnRead call, schedules a ContinueReading
// via the filter scheduler, and lets the data flow normally on subsequent reads.
//
// Verifies that NetworkFilterStatusStop genuinely pauses iteration and that
// ContinueReading from a scheduled task resumes it. Without the resume the
// upstream connection never sees the data and the test would hang.
// =============================================================================

type pauseResumeConfigFactory struct {
	shared.EmptyNetworkFilterConfigFactory
}

func (pauseResumeConfigFactory) Create(shared.NetworkFilterConfigHandle, []byte) (shared.NetworkFilterFactory, error) {
	return &pauseResumeFactory{}, nil
}

type pauseResumeFactory struct {
	shared.EmptyNetworkFilterFactory
}

func (*pauseResumeFactory) Create(handle shared.NetworkFilterHandle) shared.NetworkFilter {
	return &pauseResumeFilter{handle: handle}
}

type pauseResumeFilter struct {
	shared.EmptyNetworkFilter
	handle shared.NetworkFilterHandle
	paused bool
}

func (f *pauseResumeFilter) OnRead(shared.NetworkBuffer, bool) shared.NetworkFilterStatus {
	if !f.paused {
		f.paused = true
		// Schedule the resume on the same worker thread. The scheduler defers the
		// continuation until after this OnRead returns; otherwise ContinueReading is
		// a no-op since iteration hasn't been paused yet.
		f.handle.GetScheduler().Schedule(func() {
			f.handle.ContinueReading()
		})
		return shared.NetworkFilterStatusStop
	}
	return shared.NetworkFilterStatusContinue
}

// =============================================================================
// data_appender: appends a fixed suffix to every read buffer before letting it
// flow downstream. Verifies ReadBuffer().Append() works end-to-end (the upstream
// must observe the suffix).
// =============================================================================

type dataAppenderConfigFactory struct {
	shared.EmptyNetworkFilterConfigFactory
}

func (dataAppenderConfigFactory) Create(shared.NetworkFilterConfigHandle, []byte) (shared.NetworkFilterFactory, error) {
	return &dataAppenderFactory{}, nil
}

type dataAppenderFactory struct {
	shared.EmptyNetworkFilterFactory
}

func (*dataAppenderFactory) Create(handle shared.NetworkFilterHandle) shared.NetworkFilter {
	return &dataAppenderFilter{handle: handle}
}

type dataAppenderFilter struct {
	shared.EmptyNetworkFilter
	handle   shared.NetworkFilterHandle
	appended bool
}

func (f *dataAppenderFilter) OnRead(buf shared.NetworkBuffer, _ bool) shared.NetworkFilterStatus {
	// Append once on the first OnRead so the upstream observes the modification.
	// Subsequent reads pass through unchanged so we don't keep appending forever.
	if !f.appended {
		buf.Append([]byte("|appended"))
		f.appended = true
	}
	return shared.NetworkFilterStatusContinue
}
