Fixed a bug where a :ref:`claim_to_headers
<envoy_v3_api_field_extensions.filters.http.jwt_authn.v3.JwtProvider.claim_to_headers>` entry
whose claim could not be resolved in the JWT payload was dropped without any trace. Such an entry
is now logged at debug level.
