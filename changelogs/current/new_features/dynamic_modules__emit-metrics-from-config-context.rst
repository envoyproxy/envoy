Added support for emitting metrics (counters, gauges, and histograms) from the config context in
dynamic modules, enabling metrics to be recorded from background tasks scheduled outside the
request, connection, datagram, or log lifecycle. Supported for the HTTP filter (C++, Go, and Rust
SDKs), the network and listener filters (Go and Rust SDKs), and the UDP listener filter and access
logger (Rust SDK).
