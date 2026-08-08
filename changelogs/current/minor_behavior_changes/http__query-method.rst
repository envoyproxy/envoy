The ``QUERY`` request method, registered by `RFC 10008
<https://datatracker.ietf.org/doc/html/rfc10008>`_, is now recognized by the HTTP/1 codec and is
forwarded rather than rejected with a 400 (``HPE_INVALID_METHOD``). HTTP/2 and HTTP/3 have no
method allowlist and already forwarded it. ``QUERY`` was also added to the method registry that
:ref:`restrict_http_methods
<envoy_v3_api_field_extensions.http.header_validators.envoy_default.v3.HeaderValidatorConfig.restrict_http_methods>`
enforces. Deployments that relied on Envoy rejecting ``QUERY`` at the edge will now see those
requests routed. This behavioral change can be temporarily reverted by setting runtime guard
``envoy.reloadable_features.http1_allow_query_method`` to ``false``.
