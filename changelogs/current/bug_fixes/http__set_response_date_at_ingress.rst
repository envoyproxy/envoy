Fixed a bug where a missing or unparseable ``Date`` header on proxied upstream responses was set at
egress rather than at ingress. Filters such as :ref:`cache <config_http_filters_cache>` and
:ref:`cache_v2 <config_http_filters_cache_v2>` never saw a valid ``Date`` and could miscalculate
the response age. This behavioral change can be reverted by setting the runtime guard
``envoy.reloadable_features.http_set_response_date_at_ingress`` to ``false``.
