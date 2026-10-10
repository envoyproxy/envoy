Added :ref:`strip_alt_svc
<envoy_v3_api_field_extensions.filters.http.alternate_protocols_cache.v3.FilterConfig.strip_alt_svc>`
to the alternate protocols cache filter. When enabled, the upstream ``alt-svc`` response header is removed
after it has been recorded into the cache, so it is not forwarded to downstream clients.
