package shared

// EarlyHeaderMutation is the interface to implement your own early header
// mutation logic, which rewrites request headers before routing, tracing,
// request ID generation and any filter processing.
//
// A single instance is created once on the main thread and is shared by every
// request handled by every worker thread, so Mutate must be safe for
// concurrent use. Keep per-request state on the handle, and guard any mutable
// fields with atomics or other synchronization.
type EarlyHeaderMutation interface {
	// Mutate rewrites the request headers for one request. It is called
	// concurrently on worker threads against the one shared instance.
	//
	// The return value is NOT a success or failure indication. Returning true
	// lets Envoy continue to the next early header mutation extension in the
	// configured chain; returning false stops the chain so no later extension
	// runs. Mutations already applied are kept either way. When this is the last
	// or only extension in the chain, the return value has no effect.
	//
	// headers is the same header map as handle.RequestHeaders(), passed
	// separately for convenience. Neither may be retained beyond this call.
	Mutate(headers HeaderMap, handle EarlyHeaderMutationHandle) bool

	// OnDestroy is called when Envoy destroys the early header mutation, which
	// happens when the listener owning the connection manager configuration
	// that references it is drained and removed.
	OnDestroy()
}

// EmptyEarlyHeaderMutation is a no-op EarlyHeaderMutation that continues the
// chain. Embed it to get forward-compatible defaults for methods you don't
// care about.
type EmptyEarlyHeaderMutation struct{}

// Mutate implements EarlyHeaderMutation.
func (m *EmptyEarlyHeaderMutation) Mutate(HeaderMap, EarlyHeaderMutationHandle) bool {
	return true
}

// OnDestroy implements EarlyHeaderMutation.
func (m *EmptyEarlyHeaderMutation) OnDestroy() {}

// EarlyHeaderMutationConfigFactory parses the configuration for one early
// header mutation extension entry and builds the EarlyHeaderMutation that
// serves every request. It runs once per configured entry on the main thread.
// Implementations should be stateless and keep per-config state on the
// returned EarlyHeaderMutation.
type EarlyHeaderMutationConfigFactory interface {
	// Create parses unparsedConfig and returns the shared, thread-safe mutation
	// used for every request, or an error if the configuration is invalid.
	// Returning a nil EarlyHeaderMutation with no error is also treated as a
	// failure, and either way Envoy rejects the configuration.
	//
	// unparsedConfig contains the bytes passed via the
	// early_header_mutation_config field of the DynamicModuleEarlyHeaderMutation
	// proto. The encoding depends on the Any type used in the config, for
	// example raw bytes for BytesValue and JSON for Struct.
	Create(handle EarlyHeaderMutationConfigHandle, unparsedConfig []byte) (EarlyHeaderMutation, error)
}

// EmptyEarlyHeaderMutationConfigFactory builds an EmptyEarlyHeaderMutation.
// Useful for testing.
type EmptyEarlyHeaderMutationConfigFactory struct{}

// Create implements EarlyHeaderMutationConfigFactory.
func (f *EmptyEarlyHeaderMutationConfigFactory) Create(EarlyHeaderMutationConfigHandle,
	[]byte) (EarlyHeaderMutation, error) {
	return &EmptyEarlyHeaderMutation{}, nil
}
