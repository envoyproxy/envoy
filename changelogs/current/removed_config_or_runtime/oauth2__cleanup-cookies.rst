Removed the runtime guard ``envoy.reloadable_features.oauth2_cleanup_cookies`` and the legacy code
path it guarded. The OAuth2 filter now always removes the OAuth flow cookies (``OauthHMAC``,
``OauthExpires``, ``RefreshToken``, ``OauthNonce`` and ``CodeVerifier``, including their suffixed
names) from a request before it is forwarded upstream, so these cookies are no longer exposed to the
backend service.
