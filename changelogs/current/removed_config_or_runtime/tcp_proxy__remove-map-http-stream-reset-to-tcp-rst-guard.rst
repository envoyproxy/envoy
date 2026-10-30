Removed runtime guard ``envoy.reloadable_features.map_http_stream_reset_to_tcp_rst`` and the
legacy code path. Remote HTTP stream resets before response completion are now always mapped
to TCP resets for HTTP-tunneled TCP connections.
