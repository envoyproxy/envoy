Added :ref:`allowed_algorithms
<envoy_v3_api_field_extensions.filters.http.jwt_authn.v3.JwtProvider.allowed_algorithms>` to restrict
the signing algorithms a provider accepts. A JWT whose ``alg`` is not listed is rejected before any
key is tried.
