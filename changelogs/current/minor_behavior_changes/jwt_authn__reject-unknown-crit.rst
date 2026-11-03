jwt_authn: a JWT whose ``crit`` header is malformed, or lists a parameter not in the new
:ref:`allowed_critical_headers
<envoy_v3_api_field_extensions.filters.http.jwt_authn.v3.JwtProvider.allowed_critical_headers>`,
is now rejected, as RFC 7515 requires. This behavior can be reverted by setting runtime guard
``envoy.reloadable_features.jwt_validate_crit_header`` to ``false``.
