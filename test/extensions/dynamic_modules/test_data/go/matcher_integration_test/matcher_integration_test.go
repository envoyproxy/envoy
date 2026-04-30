// Input matcher integration test module. Mirror of the C
// matcher_check_headers fake at test_data/c/matcher_check_headers.c.
//
// Registers a matcher named "header_check" that takes the header name to inspect via
// matcher_config bytes. OnMatch returns true iff the named request header is present
// with value exactly "match".
package main

import (
	sdk "github.com/envoyproxy/envoy/source/extensions/dynamic_modules/sdk/go"
	_ "github.com/envoyproxy/envoy/source/extensions/dynamic_modules/sdk/go/abi"
	"github.com/envoyproxy/envoy/source/extensions/dynamic_modules/sdk/go/shared"
)

func init() {
	sdk.RegisterMatcherConfigFactories(map[string]shared.MatcherConfigFactory{
		"header_check": &headerCheckConfigFactory{},
	})
}

func main() {} //nolint:all

type headerCheckConfigFactory struct{}

func (headerCheckConfigFactory) Create(_ string, config []byte) (shared.Matcher, error) {
	return &headerCheckMatcher{headerName: string(config)}, nil
}

type headerCheckMatcher struct {
	shared.EmptyMatcher
	headerName string
}

func (m *headerCheckMatcher) OnMatch(ctx shared.MatchInputContext) bool {
	val, count, ok := ctx.GetHeaderValue(shared.HttpHeaderTypeRequestHeader, m.headerName, 0)
	if !ok || count == 0 {
		return false
	}
	return val.ToUnsafeString() == "match"
}
