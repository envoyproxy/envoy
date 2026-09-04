OAuth2 token cookies larger than 4096 bytes are now split across multiple cookies and rejoined when
the request comes back. Browsers reject a cookie that large, so an access, ID or refresh token above
that size was silently dropped by the client and the filter redirected the user to the authorization
server on every request. A token that still fits in one cookie is written exactly as before.

A token is split into at most 15 cookies, as ``<name>_0`` .. ``<name>_N-1`` alongside a
``<name>_chunks`` cookie holding the count. One too large for that is written as a single cookie, as
it was before, and increments ``oauth_token_cookie_oversized``. A session that splits all three
tokens may need ``max_request_headers_kb`` raised. Because a large ID token now survives where it
was previously dropped, a configured ``end_session_endpoint`` receives a correspondingly large
``id_token_hint``, which some providers may reject.

This behavior can be reverted by setting
``envoy.reloadable_features.oauth2_chunk_large_token_cookies`` to ``false``. That is worth doing
during a rolling upgrade if a client may reach an instance that predates this change, since such an
instance cannot rejoin the cookies and would restart the authorization flow. Rejoining and deleting
chunked cookies is always active regardless of the flag, so it can be turned back off without
stranding clients that already hold chunked cookies.
