//go:generate mockgen -source=program.go -destination=mocks/mock_program.go -package=mocks
package shared

import "unsafe"

// Program-wide SDK utilities. These are NOT bound to any particular extension type; they're
// thin wrappers over Envoy's process-global callbacks, accessible from any extension at any
// time (modulo per-callback thread-safety notes).
//
// Mirrors the Rust SDK's top-level module-level utilities (concurrency / validation mode /
// function & shared-data registries). Modules typically call these through the package-level
// functions exported by sdk (see sdk.GetConcurrency, sdk.IsValidationMode, etc.) — those are
// the supported entry points and are wired to the underlying ABI in abi/internal_program.go.

// ProgramHandle is the Go-facing interface for the program-wide callbacks. The default
// implementation calls into Envoy via the dynamic-modules ABI; the interface is exposed so
// modules can mock it out in tests.
type ProgramHandle interface {
	// GetConcurrency returns the number of worker threads (concurrency) configured for the
	// server. Useful inside on-program-init to size per-worker resources.
	//
	// MUST be called on the main thread.
	GetConcurrency() uint32

	// IsValidationMode reports whether the server is running in config-validation mode
	// (`--mode validate`). Modules can use this to skip expensive operations such as
	// provider lookups or loading external resources during config validation.
	//
	// MUST be called on the main thread.
	IsValidationMode() bool

	// Log writes a formatted message through Envoy's logging subsystem, gated on the
	// configured log level. Safe to call from any goroutine after program init. This is
	// the equivalent of Rust's envoy_log_* macros and is the only logging entry point
	// available to extensions whose handle does not carry a Log method (e.g. bootstrap,
	// cluster, tracer).
	Log(level LogLevel, format string, args ...any)

	// RegisterFunction registers a function pointer under the given key in the process-wide
	// function registry. This allows modules loaded in the same process to expose functions
	// that other modules resolve by name and call directly (zero-copy cross-module
	// interaction; analogous to dlsym semantics).
	//
	// Callers MUST agree on the function signature out-of-band — the registry stores opaque
	// pointers. The pointer MUST remain valid for the lifetime of the process. Returns false
	// if a function is already registered under key or fnPtr is nil.
	//
	// Thread-safe.
	RegisterFunction(key string, fnPtr unsafe.Pointer) bool

	// GetFunction retrieves a previously registered function pointer by key. Returns the
	// pointer and true if found, otherwise (nil, false). Cast the returned pointer to the
	// expected function signature.
	//
	// Thread-safe.
	GetFunction(key string) (unsafe.Pointer, bool)

	// RegisterSharedData registers an opaque data pointer under the given key in the
	// process-wide shared-data registry. Unlike the function registry, this allows
	// overwriting an existing entry — useful for patterns where shared state is refreshed
	// after a configuration reload. Callers are responsible for managing the lifetime of
	// overwritten data pointers.
	//
	// The pointer MUST remain valid for the lifetime of the process (or until the next
	// register call for the same key). Returns false if dataPtr is nil.
	//
	// Thread-safe.
	RegisterSharedData(key string, dataPtr unsafe.Pointer) bool

	// GetSharedData retrieves a previously registered data pointer by key. Returns the
	// pointer and true if found, otherwise (nil, false).
	//
	// Thread-safe.
	GetSharedData(key string) (unsafe.Pointer, bool)
}
