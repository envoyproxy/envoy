Fixed compression of empty request and response bodies when no ``content-length`` header is present.
The compressor now waits for nonempty body data before enabling compression for these streams.
This change can be temporarily reverted by setting the runtime guard
``envoy.reloadable_features.compressor_skip_empty_body`` to ``false``.
