Chunks token larger than 4 KB into multiple cookies in the client response, and combines
them when the client request token cookies are already chunked. This is to avoid redirect
loop caused by browser blocking the cookie due to size limit.
This behavior can be reverted by setting the runtime guard
``envoy.reloadable_features.oauth2_chunk_large_token_cookies`` to ``false``.
