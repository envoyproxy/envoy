dynamic modules: added ``HttpFilterEnvoyPointer`` and ``HttpFilterConfigEnvoyPointer`` to the Go SDK
``abi`` package. They return the raw Envoy pointer of an HTTP filter stream and of an HTTP filter
config, so that a Go module can pass them to native code or call an ABI callback that the Go SDK
does not wrap. They are for experts only and follow the thread and lifetime rules of the ABI.
