Added :ref:`TLS_CLIENT_AUTH
<envoy_v3_api_enum_value_extensions.http.injected_credentials.oauth2.v3.OAuth2.AuthType.TLS_CLIENT_AUTH>`
to the OAuth2 credential injector, which authenticates the client to the token endpoint using mutual
TLS, implementing OAuth 2.0 Mutual-TLS Client Authentication as defined in RFC 8705. The token
request body contains only ``grant_type`` and ``client_id``, and the client certificate is taken from
the transport socket configured on the ``token_endpoint`` cluster. :ref:`client_secret
<envoy_v3_api_field_extensions.http.injected_credentials.oauth2.v3.OAuth2.ClientCredentials.client_secret>`
is not required when this auth type is used.
