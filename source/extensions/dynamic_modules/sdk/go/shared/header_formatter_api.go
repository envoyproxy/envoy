package shared

// HeaderFormatter decides the casing of the header keys of a single HTTP/1
// message.
//
// One formatter is created per message and every method is called by the
// single Envoy worker thread that owns that message, so a formatter may keep
// mutable state without synchronization. That state is what makes the
// extension "stateful": keys seen by ProcessKey while decoding can be replayed
// by Format when encoding on the same connection.
type HeaderFormatter interface {
	// ProcessKey is called for each header key the codec receives, with the
	// casing the peer used. Headers that Envoy itself adds never reach this
	// method; Format is still called for them.
	//
	// key aliases Envoy-owned memory that is reused after this call. Since
	// remembering keys is the whole point of ProcessKey, copy it into the Go
	// heap with ToString or ToBytes before storing it.
	ProcessKey(key UnsafeEnvoyBuffer)

	// Format is called for each header key Envoy is about to serialize, in its
	// internal lower-cased form. It reports false to leave the key unchanged.
	// A formatter that only rewrites some keys should report false for the rest
	// rather than echoing them back.
	//
	// key aliases Envoy-owned memory and is only valid for the duration of this
	// call. ToUnsafeString is enough for comparisons and map lookups; use
	// ToString or ToBytes to keep it.
	//
	// The SDK hands Envoy a pointer to the string's bytes rather than copying
	// them, and Envoy copies them only after this call has returned. Keep the
	// string in a field of the formatter: a value that nothing references once
	// Format returns may be collected while Envoy is still reading it.
	Format(key UnsafeEnvoyBuffer) (string, bool)
}

// EmptyHeaderFormatter is a no-op HeaderFormatter that leaves every key
// unchanged. Embed it to get forward-compatible defaults for methods you don't
// care about.
type EmptyHeaderFormatter struct{}

// ProcessKey implements HeaderFormatter.
func (f *EmptyHeaderFormatter) ProcessKey(UnsafeEnvoyBuffer) {}

// Format implements HeaderFormatter.
func (f *EmptyHeaderFormatter) Format(UnsafeEnvoyBuffer) (string, bool) { return "", false }

// HeaderFormatterConfig produces the formatter for each HTTP/1 message.
//
// A single instance is created once on the main thread and is shared by every
// worker thread, so Create must be safe for concurrent use. Keep per-message
// state on the returned HeaderFormatter, and guard any mutable fields with
// atomics or other synchronization.
type HeaderFormatterConfig interface {
	// Create returns the formatter for one HTTP/1 message. Returning nil makes
	// Envoy use its default header casing for that message rather than failing
	// it.
	//
	// handle belongs to the formatter being created and stays valid until that
	// formatter is destroyed, so the returned value may keep it.
	Create(handle HeaderFormatterHandle) HeaderFormatter

	// OnDestroy is called when Envoy destroys the header formatter
	// configuration, which happens once the listener or cluster owning the
	// protocol options that reference it is drained and removed. Every
	// formatter this configuration produced has already been destroyed by then.
	OnDestroy()
}

// EmptyHeaderFormatterConfig is a no-op HeaderFormatterConfig that produces
// EmptyHeaderFormatter instances. Useful for testing.
type EmptyHeaderFormatterConfig struct{}

// Create implements HeaderFormatterConfig.
func (c *EmptyHeaderFormatterConfig) Create(HeaderFormatterHandle) HeaderFormatter {
	return &EmptyHeaderFormatter{}
}

// OnDestroy implements HeaderFormatterConfig.
func (c *EmptyHeaderFormatterConfig) OnDestroy() {}

// HeaderFormatterConfigFactory parses the configuration for one header
// formatter entry and builds the HeaderFormatterConfig that serves every
// message. It runs once per configured entry on the main thread.
// Implementations should be stateless and keep per-config state on the
// returned HeaderFormatterConfig.
type HeaderFormatterConfigFactory interface {
	// Create parses unparsedConfig and returns the shared, thread-safe
	// configuration used for every message, or an error if the configuration is
	// invalid. Returning a nil HeaderFormatterConfig with no error is also
	// treated as a failure, and either way Envoy rejects the configuration.
	//
	// unparsedConfig aliases the bytes passed via the header_formatter_config
	// field of the DynamicModuleHeaderFormatter proto and is only valid for the
	// duration of this call, so copy it with ToString or ToBytes to keep it. The
	// encoding depends on the Any type used in the config, for example raw bytes
	// for BytesValue and JSON for Struct.
	Create(handle HeaderFormatterConfigHandle,
		unparsedConfig UnsafeEnvoyBuffer) (HeaderFormatterConfig, error)
}

// EmptyHeaderFormatterConfigFactory builds an EmptyHeaderFormatterConfig.
// Useful for testing.
type EmptyHeaderFormatterConfigFactory struct{}

// Create implements HeaderFormatterConfigFactory.
func (f *EmptyHeaderFormatterConfigFactory) Create(HeaderFormatterConfigHandle,
	UnsafeEnvoyBuffer) (HeaderFormatterConfig, error) {
	return &EmptyHeaderFormatterConfig{}, nil
}
