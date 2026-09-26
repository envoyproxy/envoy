// Integration test module for HTTP/1 header formatter dynamic modules.
//
// The "preserve_case" configuration remembers every key the peer sent and restores that spelling
// on the way out, upper-casing keys it never saw so the test can tell the ProcessKey path from the
// Format path. The "counting" configuration exercises the requirement that one configuration
// object is shared by every worker thread, and "decline_formatter" never creates a formatter so
// the test can observe the fallback to Envoy's default casing. An unknown name returns an error,
// which makes Envoy reject the configuration.
package main

import (
	"fmt"
	"strings"
	"sync/atomic"

	sdk "github.com/envoyproxy/envoy/source/extensions/dynamic_modules/sdk/go"
	_ "github.com/envoyproxy/envoy/source/extensions/dynamic_modules/sdk/go/abi"
	"github.com/envoyproxy/envoy/source/extensions/dynamic_modules/sdk/go/shared"
)

func init() {
	sdk.RegisterHeaderFormatterConfigFactories(
		map[string]shared.HeaderFormatterConfigFactory{
			"preserve_case":     &preserveCaseConfigFactory{},
			"counting":          &countingConfigFactory{},
			"decline_formatter": &declineConfigFactory{},
		})
}

func main() {}

type preserveCaseConfigFactory struct{}

func (f *preserveCaseConfigFactory) Create(_ shared.HeaderFormatterConfigHandle,
	config shared.UnsafeEnvoyBuffer) (shared.HeaderFormatterConfig, error) {
	// The configuration bytes reach the module as an unsafe view of Envoy-owned memory, so they
	// are copied into the Go heap here: the configuration outlives this call.
	return &preserveCaseConfig{extraKey: config.ToString()}, nil
}

// preserveCaseConfig is shared by every worker thread, so it holds only immutable state.
type preserveCaseConfig struct {
	// extraKey is always upper-cased even if the peer sent it in another casing, which makes the
	// configuration bytes observable in the response.
	extraKey string
}

func (c *preserveCaseConfig) Create(
	handle shared.HeaderFormatterHandle) shared.HeaderFormatter {
	return &preserveCaseFormatter{
		observed: map[string]string{},
		extraKey: c.extraKey,
		handle:   handle,
	}
}

func (c *preserveCaseConfig) OnDestroy() {}

// preserveCaseFormatter holds per-message state: the keys this message's peer actually sent,
// indexed by their lower-cased form. Envoy calls every method from the one worker thread owning
// the message, so a plain map needs no synchronization.
type preserveCaseFormatter struct {
	observed map[string]string
	extraKey string
	// handle is handed over with the formatter and stays valid for as long as it lives, so it is
	// kept as a field here.
	handle shared.HeaderFormatterHandle
	// formatted holds the value returned by the last Format call. Envoy reads it after the hook
	// returns, so the formatter keeps it reachable until the next call replaces it.
	formatted string
}

func (f *preserveCaseFormatter) ProcessKey(key shared.UnsafeEnvoyBuffer) {
	// The handle is the module's window onto the host for this message. Only logging is exposed
	// today, and logging every key at trace level is how this module proves the handle works. The
	// enablement check keeps the formatting cost off the hot path when trace is off.
	if f.handle.IsLogLevelEnabled(shared.LogLevelTrace) {
		f.handle.Log(shared.LogLevelTrace, "header formatter observed key: %s", key.ToUnsafeString())
	}
	// The key is remembered past this call, so it is copied out of Envoy's memory first. Lowering
	// the copy keeps the map key in the Go heap too, which strings.ToLower alone would not
	// guarantee: it returns its argument unchanged when there is nothing to lower.
	owned := key.ToString()
	f.observed[strings.ToLower(owned)] = owned
}

func (f *preserveCaseFormatter) Format(key shared.UnsafeEnvoyBuffer) (string, bool) {
	// The key is only compared, looked up and upper-cased here, all of which finish before this
	// call returns, so the unsafe view is enough.
	keyView := key.ToUnsafeString()
	// Exercises the third handle accessor: a module can align its own verbosity with Envoy's.
	if f.handle.GetLogLevel() == shared.LogLevelTrace {
		f.handle.Log(shared.LogLevelTrace, "header formatter formatting key: %s", keyView)
	}
	if f.extraKey != "" && strings.EqualFold(keyView, f.extraKey) {
		f.formatted = strings.ToUpper(keyView)
		return f.formatted, true
	}
	if original, ok := f.observed[keyView]; ok {
		// Already owned by the formatter, so it outlives this call as the contract requires.
		return original, true
	}
	// Never observed, so this is a header Envoy added itself. Upper-casing it makes the two paths
	// distinguishable in the test.
	f.formatted = strings.ToUpper(keyView)
	return f.formatted, true
}

type countingConfigFactory struct{}

func (f *countingConfigFactory) Create(shared.HeaderFormatterConfigHandle,
	shared.UnsafeEnvoyBuffer) (shared.HeaderFormatterConfig, error) {
	return &countingConfig{}, nil
}

// countingConfig exercises the shared-instance requirement: one object serves every worker thread,
// so the only mutable state it keeps is an atomic.
type countingConfig struct {
	formatters atomic.Uint64
}

func (c *countingConfig) Create(shared.HeaderFormatterHandle) shared.HeaderFormatter {
	return &countingFormatter{count: c.formatters.Add(1)}
}

func (c *countingConfig) OnDestroy() {}

type countingFormatter struct {
	shared.EmptyHeaderFormatter
	count uint64
	// formatted holds the value returned by the last Format call, which Envoy reads after the hook
	// returns.
	formatted string
}

func (f *countingFormatter) Format(key shared.UnsafeEnvoyBuffer) (string, bool) {
	// Report the formatter's ordinal in a header key so the test can see how many were created.
	if key.ToUnsafeString() != "x-formatter-count" {
		return "", false
	}
	f.formatted = fmt.Sprintf("x-formatter-count-%d", f.count)
	return f.formatted, true
}

type declineConfigFactory struct{}

func (f *declineConfigFactory) Create(shared.HeaderFormatterConfigHandle,
	shared.UnsafeEnvoyBuffer) (shared.HeaderFormatterConfig, error) {
	return &declineConfig{}, nil
}

// declineConfig never creates a formatter, which must leave Envoy using its default header casing
// rather than failing the message.
type declineConfig struct{}

func (c *declineConfig) Create(shared.HeaderFormatterHandle) shared.HeaderFormatter {
	return nil
}

func (c *declineConfig) OnDestroy() {}
