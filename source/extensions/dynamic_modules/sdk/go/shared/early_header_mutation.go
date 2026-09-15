package shared

// EarlyHeaderMutationHandle is the per-request handle passed to
// EarlyHeaderMutation.Mutate. It exposes the request headers and read-only
// access to the stream info.
//
// A handle is valid only for the duration of one Mutate call and must not be
// stored or shared across goroutines. Because Mutate runs concurrently on
// worker threads against one shared EarlyHeaderMutation, the handle is the
// only per-request state and is passed in as an argument rather than captured
// when the mutation is created.
//
// Only the request headers exist at this point in the request lifecycle. The
// stream info is read-only: connection-level attributes and any dynamic
// metadata or filter state published by listener or network filters are
// available, while the route, the response and every upstream attribute are
// not populated yet and their getters report false.
type EarlyHeaderMutationHandle interface {
	// RequestHeaders returns the mutable request headers for this request. It is
	// the same header map that Mutate receives as its first argument.
	RequestHeaders() HeaderMap

	// GetAttributeString retrieves a string attribute of the stream. It reports
	// false when the attribute is absent, unsupported, or not a string.
	// NOTE: The memory of the returned buffer is managed by Envoy and is only
	// valid until the end of the current Mutate call. Copy it to keep it.
	GetAttributeString(attributeID AttributeID) (UnsafeEnvoyBuffer, bool)

	// GetAttributeInt retrieves an integer attribute of the stream. It reports
	// false when the attribute is absent, unsupported, or not an integer.
	GetAttributeInt(attributeID AttributeID) (uint64, bool)

	// GetAttributeBool retrieves a boolean attribute of the stream. It reports
	// false when the attribute is absent, unsupported, or not a boolean.
	GetAttributeBool(attributeID AttributeID) (bool, bool)

	// GetDynamicMetadataString retrieves a string value from dynamic metadata.
	// filterName is the filter namespace (for example
	// "envoy.filters.http.dynamic_module") and path is the key within that
	// namespace, which may be a dotted path into nested values. Only string
	// values are returned.
	// NOTE: The memory of the returned buffer is managed by Envoy and is only
	// valid until the end of the current Mutate call. Copy it to keep it.
	GetDynamicMetadataString(filterName, path string) (UnsafeEnvoyBuffer, bool)

	// GetDynamicMetadataNumber retrieves a number value from dynamic metadata.
	// The arguments are the same as GetDynamicMetadataString.
	GetDynamicMetadataNumber(filterName, path string) (float64, bool)

	// GetDynamicMetadataBool retrieves a boolean value from dynamic metadata.
	// The arguments are the same as GetDynamicMetadataString.
	GetDynamicMetadataBool(filterName, path string) (bool, bool)

	// GetFilterState retrieves the raw bytes of a filter state value. Only
	// objects stored as string accessors, which is what the dynamic module
	// filter state setters create, are readable.
	// NOTE: The memory of the returned buffer is managed by Envoy and is only
	// valid until the end of the current Mutate call. Copy it to keep it.
	GetFilterState(key string) (UnsafeEnvoyBuffer, bool)

	// Log writes a message to Envoy's logger at the given level.
	Log(level LogLevel, format string, args ...any)

	// GetLogLevel returns the current effective log level of Envoy's logger.
	GetLogLevel() LogLevel

	// IsLogLevelEnabled reports whether the given log level is enabled.
	IsLogLevelEnabled(level LogLevel) bool
}

// EarlyHeaderMutationConfigHandle is passed to
// EarlyHeaderMutationConfigFactory.Create and gives the factory access to host
// services while the mutation is being built on the main thread.
type EarlyHeaderMutationConfigHandle interface {
	// Log writes a message to Envoy's logger at the given level.
	Log(level LogLevel, format string, args ...any)

	// GetLogLevel returns the current effective log level of Envoy's logger.
	GetLogLevel() LogLevel

	// IsLogLevelEnabled reports whether the given log level is enabled.
	IsLogLevelEnabled(level LogLevel) bool
}
