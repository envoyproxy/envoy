When a new SDS watch is added for a secret name that another watch on the same delta gRPC mux
(or SotW gRPC mux with ``envoy.reloadable_features.unified_mux`` enabled) already subscribes to,
and the new watch has not received any update yet, Envoy now delivers the last received version of
that secret to the new watch instead of waiting for the management server to send it again.
Previously the new watch received nothing until its ``initial_fetch_timeout`` expired or the server
pushed a new version, for example after a listener or cluster update that only changed an
``sds_config``. Other resource types are not cached. This behavior can be temporarily reverted by
setting the runtime flag ``envoy.reloadable_features.xds_replay_cached_resources`` to ``false``.
