The prometheus formatter now strips a registered custom stat namespace (e.g. ``wasmcustom``) when
it appears as a non-leading, non-trailing segment of a tag-extracted stat name, so that custom
metrics created on a non-root scope render with the standard ``envoy_`` prefix and without the
internal namespace. For example an upstream Wasm counter that previously rendered as
``envoy_cluster_wasmcustom_upstream_rq_2xx`` now renders as ``envoy_cluster_upstream_rq_2xx``,
matching listener Wasm and native cluster metrics. The ``cluster_name`` and other extracted labels
are preserved. This primarily fixes upstream HTTP filter Wasm metrics emitted under cluster scope.
This behavioral change can be temporarily reverted by setting runtime guard
``envoy.reloadable_features.strip_scoped_custom_stat_namespace`` to ``false``.
