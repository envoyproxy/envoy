Added support for the :ref:`alternate_protocols_cache <extension_envoy.filters.http.alternate_protocols_cache>`
filter in the upstream HTTP filter chain. A cluster can now list it in its ``http_filters``. It caches ``alt-svc`` responses
only when the cluster also sets ``alternate_protocols_cache_options``.
