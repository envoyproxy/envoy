Added two optional cache bounds to the on-demand secret certificate selector: :ref:`max_secrets
<envoy_v3_api_field_extensions.transport_sockets.tls.cert_selectors.on_demand_secret.v3.Config.max_secrets>`
caps the number of fetched and cached secrets — when the cache is full, admitting a new secret first
reclaims an entry with no certificate and no pending handshakes (``cert_reclaimed`` counter), and
otherwise the handshake is rejected (``cert_overflow`` counter) — and :ref:`cache_idle_timeout
<envoy_v3_api_field_extensions.transport_sockets.tls.cert_selectors.on_demand_secret.v3.Config.cache_idle_timeout>`
evicts cached secrets unused by any TLS handshake for the configured duration (``cert_evicted``
counter). Both are disabled by default and are recommended together when peers control the mapped
secret name, e.g. via SNI.
