When a new xDS watch is added for a resource name that another watch on the same delta gRPC mux
(or SotW gRPC mux with ``envoy.reloadable_features.unified_mux`` enabled) already subscribes to,
Envoy now delivers the last received version of that resource to the new
watch instead of waiting for the management server to send it again. Previously the new watch
received nothing until its ``initial_fetch_timeout`` expired or the server pushed a new version,
for example after a listener or cluster update that only changed an ``sds_config``. Resources of
wildcard subscriptions are not cached. This behavior can be temporarily reverted by setting the
runtime flag ``envoy.reloadable_features.xds_replay_cached_resources`` to ``false``.
