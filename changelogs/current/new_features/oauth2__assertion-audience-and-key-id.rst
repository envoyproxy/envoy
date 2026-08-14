Added :ref:`assertion_audience
<envoy_v3_api_field_extensions.filters.http.oauth2.v3.PrivateKeyJwtConfig.assertion_audience>` and
:ref:`key_id <envoy_v3_api_field_extensions.filters.http.oauth2.v3.PrivateKeyJwtConfig.key_id>` to
the OAuth2 filter's private key JWT configuration. ``assertion_audience`` overrides the ``aud``
claim in the JWT client assertion (for example with the authorization server's issuer identifier,
which some identity providers require), and ``key_id`` sets the ``kid`` header of the assertion so
the identity provider can select the correct verification key. If unset, the ``aud`` claim
defaults to the token endpoint URI as before, and no ``kid`` header is emitted.