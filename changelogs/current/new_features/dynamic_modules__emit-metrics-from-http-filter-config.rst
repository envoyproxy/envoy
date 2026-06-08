Added support for emitting metrics (counters, gauges, and histograms) from the HTTP filter config
context in dynamic modules, enabling metrics to be recorded from background tasks scheduled outside
the request lifecycle. Available in the C++, Go, and Rust SDKs.
