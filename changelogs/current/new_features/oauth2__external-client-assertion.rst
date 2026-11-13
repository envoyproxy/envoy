Added ``CLIENT_ASSERTION`` client authentication to the OAuth2 filter, allowing externally issued JWTs
such as Kubernetes service account tokens to be supplied through
:ref:`token_secret <envoy_v3_api_field_extensions.filters.http.oauth2.v3.OAuth2Credentials.token_secret>`
for authorization code exchanges and refresh token requests.
