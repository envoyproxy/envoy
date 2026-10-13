Fixed a crash where a panic inside a Rust dynamic module bootstrap stats visitor passed to
``iterate_counters`` or ``iterate_gauges`` unwound across the C ABI boundary and aborted Envoy. The
SDK now catches the panic, stops the iteration, and logs the failure so the process stays up.
