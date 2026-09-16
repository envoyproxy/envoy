Added :ref:`assertion_audience
<envoy_v3_api_field_extensions.filters.http.oauth2.v3.PrivateKeyJwtConfig.assertion_audience>` to
the OAuth2 filter's private key JWT configuration, setting the ``aud`` claim of the client
assertion (for example to the authorization server's issuer identifier, which some identity
providers require). If unset, the ``aud`` claim remains the configured ``token_endpoint`` URI.
The :ref:`token_secret
<envoy_v3_api_field_extensions.filters.http.oauth2.v3.OAuth2Credentials.token_secret>` may now also
be supplied as a multi-entry generic secret with ``private_key`` and ``key_id`` entries; when a
``key_id`` entry is present, its value is set as the ``kid`` header parameter of the client
assertion and is rotated together with the signing key. Existing single-value secrets are
unaffected and emit no ``kid`` header.
