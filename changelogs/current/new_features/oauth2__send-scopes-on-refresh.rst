Added :ref:`send_scopes_on_refresh <envoy_v3_api_field_extensions.filters.http.oauth2.v3.OAuth2Config.send_scopes_on_refresh>`
to include the configured ``auth_scopes`` as the ``scope`` parameter of refresh token requests, which identity providers such as
Microsoft Entra ID require when the OAuth2 client is also the resource.
