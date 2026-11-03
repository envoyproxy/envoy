Added :ref:`critical_headers
<envoy_v3_api_field_extensions.filters.http.jwt_authn.v3.JwtProvider.critical_headers>` to reject a
JWT whose ``crit`` header is malformed or lists an extension parameter the upstream does not
handle, as RFC 7515 requires. If not set, ``crit`` is ignored as before.
