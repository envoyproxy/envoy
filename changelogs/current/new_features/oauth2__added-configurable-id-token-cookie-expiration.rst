Added :ref:`use_access_token_expiry_for_id_token_cookie
<envoy_v3_api_field_extensions.filters.http.oauth2.v3.OAuth2Config.use_access_token_expiry_for_id_token_cookie>`
to the OAuth2 filter, allowing the ID token cookie lifetime to be set from the ``expires_in``
field of the access token response rather than from the ``exp`` claim in the ID token JWT.
This is useful when the access token response advertises a longer lifetime than the ID token.
