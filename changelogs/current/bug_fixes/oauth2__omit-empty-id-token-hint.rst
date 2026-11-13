Fixed OIDC logout redirects to omit ``id_token_hint`` when the ID Token cookie is missing or empty,
instead of sending an empty value that some OpenID Providers reject. Logout requests continue to
include ``client_id`` and preserve the configured ``post_logout_redirect_uri`` behavior.
