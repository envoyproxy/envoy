Removed runtime guard
``envoy.reloadable_features.propagate_upstream_rst_through_tunneled_tcp_proxy`` and the legacy
code path. Upstream TCP resets are now always propagated to the downstream connection.
