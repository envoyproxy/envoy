Fixed upstream TLS client session caching so sessions are scoped by the effective SNI used
for the connection. This prevents a session learned for one upstream SNI from being offered
on a connection using a different SNI. The existing ``max_session_keys`` setting continues
to limit the total number of cached sessions. This behavior can be temporarily reverted by
setting runtime guard
``envoy.reloadable_features.scope_upstream_tls_session_cache_by_sni`` to ``false``.
