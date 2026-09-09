Added :ref:`MTLS_AUTH
<envoy_v3_api_enum_value_extensions.http.injected_credentials.oauth2.v3.OAuth2.AuthType.MTLS_AUTH>`
to the OAuth2 credential injector, which authenticates the client to the token endpoint using mutual
TLS as described in `RFC 8705 <https://www.rfc-editor.org/rfc/rfc8705>`_. The token request body
contains only ``grant_type`` and ``client_id``, and the client certificate is taken from the
``transport_socket`` configured on the token endpoint cluster. :ref:`client_secret
<envoy_v3_api_field_extensions.http.injected_credentials.oauth2.v3.OAuth2.ClientCredentials.client_secret>`
is not required when this auth type is used.
