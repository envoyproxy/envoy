Fixed upstream TLS client session caching to prefer sessions from the same resolved IP address and
port within each SNI. On an exact endpoint miss, Envoy still tries the newest session for the same
SNI so servers with shared ticket keys retain cross-endpoint reuse. The ``max_session_keys`` setting
remains a global limit, so operators must configure enough keys for their endpoint count and ticket
depth. This behavior can be temporarily reverted to SNI-only selection by setting runtime guard
``envoy.reloadable_features.scope_upstream_tls_session_cache_by_endpoint`` to ``false``.
