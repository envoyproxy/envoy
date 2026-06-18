Upstream HTTP Wasm filter stats now use the server-wide root stats scope instead of the
cluster stats scope, matching downstream HTTP Wasm filter scoping. For a custom counter ``foo``
in the ``wasmcustom`` namespace, admin ``/stats`` output changes from
``cluster.<cluster_name>.wasmcustom.foo`` to ``wasmcustom.foo``. Prometheus output changes from
``envoy_cluster_wasmcustom_foo{envoy_cluster_name="X"}`` to ``foo`` because the leading registered
custom namespace is stripped by the Prometheus formatter. Plugins that need per-cluster
differentiation should encode the cluster name into the metric name themselves. Can be reverted
by setting runtime guard ``envoy.reloadable_features.upstream_wasm_filter_uses_root_scope`` to
``false``.
