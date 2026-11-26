The :ref:`alternate protocols cache filter <envoy_v3_api_msg_extensions.filters.http.alternate_protocols_cache.v3.FilterConfig>`
can now be configured as an upstream HTTP filter on a cluster, so HTTP/3 discovery through ``alt-svc`` can be
turned on per cluster without adding the filter to every listener.
