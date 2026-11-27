The :ref:`JWT Authentication filter <config_http_filters_jwt_authn>` now treats a header value
containing multiple comma-joined credentials as equivalent to the same header sent on multiple lines,
as required by RFC 9110 section 5.3, and extracts each credential as a separate token. Previously the
whole value was extracted as a single token. This behavior can be reverted by setting the runtime guard
``envoy.reloadable_features.jwt_authn_split_comma_joined_headers`` to ``false``.
