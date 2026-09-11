QUIC downstream filter chains that require a client certificate now scope TLS session resumption to
the filter chain configuration, mirroring the TCP TLS session id context. A session is only resumed
under the configuration that established it. This behavior can be reverted by setting the runtime
guard ``envoy.reloadable_features.quic_reject_cross_config_session_resumption`` to ``false``.
