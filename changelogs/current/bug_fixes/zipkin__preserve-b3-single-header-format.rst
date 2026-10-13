Fixed the Zipkin tracer to propagate the trace context with the B3 single ``b3`` header when the downstream
request uses it, instead of always injecting the multiple ``x-b3-*`` headers and forwarding the stale ``b3``
header upstream. This behavior can be reverted by setting the runtime guard
``envoy.reloadable_features.zipkin_preserve_b3_single_header_format`` to ``false``.
