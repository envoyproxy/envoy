Added an optional :ref:`max_secrets
<envoy_v3_api_field_extensions.transport_sockets.tls.cert_selectors.on_demand_secret.v3.Config.max_secrets>`
limit to the on-demand secret certificate selector, capping the number of fetched and cached secrets.
When the cache is full, admitting a new secret first reclaims an entry with no certificate and no
pending handshakes (``cert_reclaimed`` counter); otherwise the handshake is rejected
(``cert_overflow`` counter). Unset preserves the previous unbounded behavior.
