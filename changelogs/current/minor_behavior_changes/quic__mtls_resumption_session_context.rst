QUIC downstream filter chains that require a client certificate now scope TLS session resumption to
the filter chain configuration, mirroring the TCP TLS session id context. A session is only resumed
under the configuration that established it.
