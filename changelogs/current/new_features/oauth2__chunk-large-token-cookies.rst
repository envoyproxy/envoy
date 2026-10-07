Added :ref:`token_cookie_chunking
<envoy_v3_api_field_extensions.filters.http.oauth2.v3.OAuth2Config.token_cookie_chunking>` to the
OAuth2 filter. When set, an access, ID or refresh token cookie larger than ``max_cookie_size``
(default 4096 bytes, the limit browsers store) is split across up to ``max_chunks`` cookies
(default 8) and rejoined on the next request. Without it, the browser drops such a cookie and the
user is redirected to the authorization server on every request. Chunked cookies are always read,
so the setting can be removed safely. Added the ``oauth_token_cookie_chunked``,
``oauth_token_cookie_reassembled``, ``oauth_token_cookie_malformed_chunks`` and
``oauth_token_cookie_oversized`` counters.
