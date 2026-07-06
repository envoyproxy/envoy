Added support for always-reject behavior in the :ref:`local rate limit filter
<config_http_filters_local_rate_limit>` by setting ``max_tokens`` to ``0`` in the
:ref:`token bucket <envoy_v3_api_msg_type.v3.TokenBucket>` configuration. This applies to both
the default token bucket and per-descriptor token buckets, including wildcard (dynamic) descriptors.
When ``max_tokens`` is ``0``, the fill interval validation (minimum 50ms) is also skipped since
filling is irrelevant.
