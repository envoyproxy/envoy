Added :ref:`dynamic metadata <config_http_filters_body_size_limit_dynamic_metadata>` to the body
size limit filter. When a request is rejected for exceeding the configured limit, the filter emits a
``details`` field describing the observed request length and the configured limit. The metadata is
stored under the namespace equal to the name of the filter in the filter chain.
