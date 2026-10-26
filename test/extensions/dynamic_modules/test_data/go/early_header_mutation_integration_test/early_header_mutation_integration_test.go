// Integration test module for early header mutation dynamic modules.
//
// The mutations registered here exercise the full handle ABI surface: header reads (single value,
// indexed multi-value, count, bulk iteration), header mutation (set, add, remove), stream info
// attributes (string, int, bool), dynamic metadata (string, number, bool) and filter state bytes.
//
// Two names differ only in their chain-continuation return value so the integration test can
// observe that false stops the chain while true lets the next extension run. A third name counts
// invocations through an atomic, exercising the requirement that one mutation object is shared by
// every worker thread. An unknown name returns an error so the config test can assert rejection.
package main

import (
	"fmt"
	"sort"
	"strconv"
	"strings"
	"sync/atomic"

	sdk "github.com/envoyproxy/envoy/source/extensions/dynamic_modules/sdk/go"
	_ "github.com/envoyproxy/envoy/source/extensions/dynamic_modules/sdk/go/abi"
	"github.com/envoyproxy/envoy/source/extensions/dynamic_modules/sdk/go/shared"
)

func init() {
	sdk.RegisterEarlyHeaderMutationConfigFactories(
		map[string]shared.EarlyHeaderMutationConfigFactory{
			"test_mutation": &testMutationConfigFactory{continueChain: true},
			"stop_chain":    &testMutationConfigFactory{continueChain: false},
			"counting":      &countingConfigFactory{},
		})
}

func main() {}

type testMutationConfigFactory struct {
	continueChain bool
}

func (f *testMutationConfigFactory) Create(_ shared.EarlyHeaderMutationConfigHandle,
	config []byte) (shared.EarlyHeaderMutation, error) {
	// The configuration bytes reach the module through the factory and are copied here because
	// Envoy owns the backing memory only for the duration of this call.
	return &testMutation{marker: string(config), continueChain: f.continueChain}, nil
}

// testMutation rewrites the request headers and reports back what it observed through the handle.
type testMutation struct {
	shared.EmptyEarlyHeaderMutation
	marker        string
	continueChain bool
}

func (m *testMutation) Mutate(headers shared.HeaderMap,
	handle shared.EarlyHeaderMutationHandle) bool {
	// Header read: echo a single value back under a different key. Values read from Envoy are
	// copied before any setter runs, because a setter may invalidate them.
	if input := headers.Get("x-test-input"); len(input) > 0 {
		headers.Set("x-dynamic-module-echo", input[0].ToString())
	}

	// Header read: count and bulk iteration. The key list is sorted so the assertion is stable
	// regardless of header map ordering.
	all := headers.GetAll()
	keys := make([]string, 0, len(all))
	for _, header := range all {
		keys = append(keys, header[0].ToString())
	}
	sort.Strings(keys)
	headers.Set("x-dynamic-module-header-count", strconv.Itoa(len(all)))
	headers.Set("x-dynamic-module-keys", strings.Join(keys, ","))

	// Header mutation: add builds a multi-value header, which Get then reports in full.
	headers.Add("x-dynamic-module-added", "one")
	headers.Add("x-dynamic-module-added", "two")
	if added := headers.Get("x-dynamic-module-added"); len(added) > 1 {
		headers.Set("x-dynamic-module-multi",
			fmt.Sprintf("%s:%d", added[1].ToString(), len(added)))
	}

	// Header mutation: remove.
	headers.Remove("x-remove-me")
	headers.Set("x-dynamic-module-removed", yesNo(len(headers.Get("x-remove-me")) == 0))

	// Stream info: attributes.
	if protocol, ok := handle.GetAttributeString(shared.AttributeIDRequestProtocol); ok {
		headers.Set("x-dynamic-module-protocol", protocol.ToString())
	}
	// ConnectionId is populated at connection establishment, so it is available this early.
	if _, ok := handle.GetAttributeInt(shared.AttributeIDConnectionId); ok {
		headers.Set("x-dynamic-module-connection-id-present", "yes")
	}
	if mtls, ok := handle.GetAttributeBool(shared.AttributeIDConnectionMTLS); ok {
		headers.Set("x-dynamic-module-mtls", yesNo(mtls))
	}

	// Stream info: the route and response attributes are not populated this early, so this getter
	// must report absence rather than a stale value.
	_, responseCodePresent := handle.GetAttributeInt(shared.AttributeIDResponseCode)
	headers.Set("x-dynamic-module-response-code-absent", yesNo(!responseCodePresent))

	// Stream info: dynamic metadata and filter state are readable but not writable here.
	if value, ok := handle.GetDynamicMetadataString("envoy.test.early", "key"); ok {
		headers.Set("x-dynamic-module-metadata", value.ToString())
	}
	if value, ok := handle.GetDynamicMetadataNumber("envoy.test.early", "number"); ok {
		headers.Set("x-dynamic-module-metadata-number", strconv.FormatFloat(value, 'g', -1, 64))
	}
	if value, ok := handle.GetDynamicMetadataBool("envoy.test.early", "flag"); ok {
		headers.Set("x-dynamic-module-metadata-bool", yesNo(value))
	}
	if value, ok := handle.GetFilterState("envoy.test.early.state"); ok {
		headers.Set("x-dynamic-module-filter-state", value.ToString())
	}

	headers.Set("x-dynamic-module-marker", m.marker)

	return m.continueChain
}

type countingConfigFactory struct{}

func (f *countingConfigFactory) Create(shared.EarlyHeaderMutationConfigHandle,
	[]byte) (shared.EarlyHeaderMutation, error) {
	return &countingMutation{}, nil
}

// countingMutation exercises the shared-instance requirement: one object serves every worker
// thread, so the only mutable state it keeps is an atomic.
type countingMutation struct {
	shared.EmptyEarlyHeaderMutation
	calls atomic.Uint64
}

func (m *countingMutation) Mutate(headers shared.HeaderMap,
	_ shared.EarlyHeaderMutationHandle) bool {
	headers.Set("x-dynamic-module-calls", strconv.FormatUint(m.calls.Add(1), 10))
	return true
}

func yesNo(value bool) string {
	if value {
		return "yes"
	}
	return "no"
}
