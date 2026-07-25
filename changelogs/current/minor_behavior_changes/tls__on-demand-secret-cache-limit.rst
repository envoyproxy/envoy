The on-demand secret certificate selector now limits the number of fetched and cached secrets to 1024 by
default, where previously the cache was unbounded. Handshakes that require fetching a new secret once the
cache is full are rejected and counted in the ``cert_overflow`` counter. The limit can be adjusted with
:ref:`max_secrets
<envoy_v3_api_field_extensions.transport_sockets.tls.cert_selectors.on_demand_secret.v3.Config.max_secrets>`;
deployments that require more than 1024 concurrently cached secrets should raise it accordingly (setting
it to a very large value restores the previous unbounded behavior).
