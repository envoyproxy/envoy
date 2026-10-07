Added :ref:`chunk_large_token_cookies
<envoy_v3_api_field_extensions.filters.http.oauth2.v3.OAuth2Config.chunk_large_token_cookies>` to the
OAuth2 filter. When enabled, an access, ID or refresh token cookie larger than the 4096 bytes a
browser stores is split across up to 8 cookies and rejoined on the next request. Without it, the
browser drops such a cookie and the user is redirected to the authorization server on every
request. Chunked cookies are always read, so the option can be turned off safely. Added the
``oauth_token_cookie_chunked``, ``oauth_token_cookie_reassembled``,
``oauth_token_cookie_malformed_chunks`` and ``oauth_token_cookie_oversized`` counters.
