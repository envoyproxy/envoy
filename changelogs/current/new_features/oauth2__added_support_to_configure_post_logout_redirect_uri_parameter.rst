Added :ref:`post_logout_redirect_uri
<envoy_v3_api_field_extensions.filters.http.oauth2.v3.OAuth2Config.post_logout_redirect_uri>`
to control the ``post_logout_redirect_uri`` query parameter sent to the OpenID Connect
RP-Initiated Logout ``end_session_endpoint``. Set :ref:`uri
<envoy_v3_api_field_extensions.filters.http.oauth2.v3.PostLogoutRedirectUri.uri>` (supports header
formatting tokens) to send a custom value, or :ref:`disabled
<envoy_v3_api_field_extensions.filters.http.oauth2.v3.PostLogoutRedirectUri.disabled>` to omit the
parameter entirely.
