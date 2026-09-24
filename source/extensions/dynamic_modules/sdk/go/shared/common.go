package shared

// CommonHandle exposes the host callbacks that are process-wide rather than tied to any single
// Envoy object. It is embedded into the per-extension config handles instead of being reached
// through a package-level global, so that these methods are only in scope where the host can
// actually service them, and so that a test can supply them through the same generated mock it
// already uses for the rest of the handle.
//
// All of the methods read from the Envoy runtime: the layered key/value configuration described by
// the layered_runtime bootstrap option, including RTDS layers and values set through the admin
// /runtime_modify endpoint.
//
// NOTE: Envoy reaches the runtime through a server context that only exists on the main thread.
// That is why these methods live on the config handles, which are used while a config is being
// built on the main thread, and not on the per-request or per-stream handles, which run on worker
// threads where the runtime is not reachable. Read the values you need while building your config
// and cache them there.
type CommonHandle interface {
	// GetRuntimeBool reads a runtime value as a boolean, returning defaultValue when the key does
	// not exist or the stored value is not a boolean.
	GetRuntimeBool(key string, defaultValue bool) bool

	// GetRuntimeInt reads a runtime value as an unsigned integer, returning defaultValue when the
	// key does not exist or the stored value is not an integer.
	//
	// Envoy stores every numeric runtime value as a double, so this conversion is lossy at both
	// ends: a value above 2^53 is rounded to the nearest representable value and a fractional value
	// is truncated toward zero, and in both cases the converted value is returned rather than
	// defaultValue. Only a negative value, or one beyond the range of a uint64, yields
	// defaultValue. Use GetRuntimeNumber to read the value without either conversion.
	GetRuntimeInt(key string, defaultValue uint64) uint64

	// GetRuntimeNumber reads a runtime value as a float64, returning defaultValue when the key does
	// not exist or the stored value is not a number.
	//
	// This is the lossless counterpart to GetRuntimeInt: it returns the value exactly as Envoy
	// stores it, so it neither rounds nor truncates, and it reads negative values, which
	// GetRuntimeInt answers with its default.
	GetRuntimeNumber(key string, defaultValue float64) float64
}
