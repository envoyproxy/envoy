package shared

// HeaderFormatterHandle is the per-message handle passed to
// HeaderFormatterConfig.Create.
//
// Header formatting happens in the HTTP/1 codec, below the filter chain, so
// there is no stream info, dynamic metadata or filter state to expose. The
// handle therefore only carries logging, and is where any future
// formatter-scoped callback will live.
//
// A handle is created with the formatter and lives exactly as long as it does,
// so a formatter is free to keep the one it is handed. It belongs to the single
// worker thread that owns the message and must not be shared across
// goroutines.
type HeaderFormatterHandle interface {
	// Log writes a message to Envoy's logger at the given level.
	Log(level LogLevel, format string, args ...any)

	// GetLogLevel returns the current effective log level of Envoy's logger.
	GetLogLevel() LogLevel

	// IsLogLevelEnabled reports whether the given log level is enabled.
	IsLogLevelEnabled(level LogLevel) bool
}

// HeaderFormatterConfigHandle is passed to
// HeaderFormatterConfigFactory.Create and gives the factory access to host
// services while the configuration is being built on the main thread.
//
// Header formatting happens in the HTTP/1 codec, below the filter chain, so
// there is no stream info, dynamic metadata or filter state to expose. The
// handle therefore only carries logging.
type HeaderFormatterConfigHandle interface {
	// Log writes a message to Envoy's logger at the given level.
	Log(level LogLevel, format string, args ...any)

	// GetLogLevel returns the current effective log level of Envoy's logger.
	GetLogLevel() LogLevel

	// IsLogLevelEnabled reports whether the given log level is enabled.
	IsLogLevelEnabled(level LogLevel) bool
}
