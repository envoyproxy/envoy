Added :ref:`post_logout_redirect_uri
<envoy_v3_api_field_extensions.filters.http.oauth2.v3.OAuth2Config.post_logout_redirect_uri>`
and :ref:`disable_post_logout_redirect_uri
<envoy_v3_api_field_extensions.filters.http.oauth2.v3.OAuth2Config.disable_post_logout_redirect_uri>`
to override or omit the ``post_logout_redirect_uri`` query parameter sent to the OpenID Connect
RP-Initiated Logout ``end_session_endpoint``.