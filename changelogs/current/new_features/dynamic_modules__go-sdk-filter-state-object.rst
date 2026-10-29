dynamic modules: added ``SetFilterStateObject`` and ``GetFilterStateObject`` to the Go SDK HTTP
filter handle. A Go module can now store a Go value in filter state and read it back from any of
its filters on the same stream, without serializing it. A value that implements
``FilterStateObjectDestroyer`` is notified when Envoy destroys the entry.
