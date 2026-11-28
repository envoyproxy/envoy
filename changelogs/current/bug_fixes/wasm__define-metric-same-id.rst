Fixed a bug where each ``define_metric`` call from a Wasm module allocated a new metric id and host
memory, even for a metric that was already defined. Modules that define their metrics on every
request grew Envoy's memory with the request volume. Defining a metric with the same type and name
now returns the same metric id. Also fixed undefined behavior when a gauge is incremented by the
minimum ``int64`` value.
