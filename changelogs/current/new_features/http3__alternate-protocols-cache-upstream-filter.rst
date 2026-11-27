The :ref:`alternate protocols cache filter <envoy_v3_api_msg_extensions.filters.http.alternate_protocols_cache.v3.FilterConfig>`
can now be configured as an upstream HTTP filter, in a cluster's ``http_filters`` or the router's ``upstream_http_filters``.
It caches ``alt-svc`` responses only when the cluster also sets ``alternate_protocols_cache_options``. As an upstream
filter it also caches ``alt-svc`` from responses the router discards and retries.
