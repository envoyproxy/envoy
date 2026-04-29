//go:generate mockgen -source=matcher.go -destination=mocks/mock_matcher.go -package=mocks
package shared

// Input matcher SDK surface for dynamic modules.
//
// Mirrors the Rust SDK's `matcher` module. A matcher evaluates HTTP request/response data and
// returns a boolean match result. Modules expose a matcher by implementing
// MatcherConfigFactory and registering it from an init() function via
// sdk.RegisterMatcherConfigFactories.
//
// Lifecycle: Envoy calls MatcherConfigFactory.Create exactly once per matcher configuration to
// build a Matcher. The Matcher's OnMatch is then invoked on worker threads for each match
// evaluation. The MatchInputContext passed to OnMatch is valid only for the duration of the
// callback; do not retain it.

// Matcher is the module-side matcher object. Implementations must be safe to call from any
// worker thread because OnMatch is called concurrently.
type Matcher interface {
	// OnMatch is called on worker threads to evaluate whether the input matches. The ctx
	// provides access to HTTP headers and other matching data and is valid only for the
	// duration of this call. Returns true to signal a match, false otherwise.
	OnMatch(ctx MatchInputContext) bool

	// OnDestroy is called when the matcher configuration is destroyed.
	OnDestroy()
}

// EmptyMatcher is a no-op Matcher that always returns false.
type EmptyMatcher struct{}

func (*EmptyMatcher) OnMatch(_ MatchInputContext) bool { return false }
func (*EmptyMatcher) OnDestroy()                       {}

// MatcherConfigFactory is the top-level factory the module registers via
// sdk.RegisterMatcherConfigFactories.
type MatcherConfigFactory interface {
	// Create parses unparsedConfig and returns a Matcher. Returning a non-nil error rejects
	// the configuration.
	Create(name string, unparsedConfig []byte) (Matcher, error)
}

// EmptyMatcherConfigFactory is a no-op MatcherConfigFactory.
type EmptyMatcherConfigFactory struct{}

func (*EmptyMatcherConfigFactory) Create(_ string, _ []byte) (Matcher, error) {
	return &EmptyMatcher{}, nil
}

// MatchInputContext is the per-match handle passed to Matcher.OnMatch. It provides read-only
// access to the HTTP request/response data being matched against. Valid only for the duration of
// the OnMatch callback.
type MatchInputContext interface {
	// GetHeadersSize returns the number of headers in the specified header map. Supported
	// types are RequestHeader, ResponseHeader, and ResponseTrailer.
	GetHeadersSize(headerType HttpHeaderType) uint64

	// GetHeaders returns all headers from the specified header map.
	//
	// NOTE: The buffers are owned by Envoy and only valid for the duration of the callback.
	// Copy if you need to keep them.
	GetHeaders(headerType HttpHeaderType) [][2]UnsafeEnvoyBuffer

	// GetHeaderValue returns a header value by key. index selects among multi-value headers
	// (0 = first); the second return value is the total count of values for that key (0 when
	// the header is missing).
	GetHeaderValue(headerType HttpHeaderType, key string, index uint64) (UnsafeEnvoyBuffer, uint64, bool)
}
