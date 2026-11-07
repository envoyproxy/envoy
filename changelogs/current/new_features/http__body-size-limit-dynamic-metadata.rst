Added :ref:`dynamic metadata <config_http_filters_body_size_limit_dynamic_metadata>` to the body
size limit filter. When a request is rejected for exceeding the configured limit, the filter emits a
``details`` field under the ``envoy.filters.http.body_size_limit`` namespace describing the observed
request length and the configured limit.
