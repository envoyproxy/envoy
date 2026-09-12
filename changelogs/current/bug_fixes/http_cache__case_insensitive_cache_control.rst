http cache: fixed cache and cache_v2 to ignore case in Cache-Control directive names, as required by
`RFC 9111 section 5.2 <https://www.rfc-editor.org/rfc/rfc9111.html#section-5.2>`_. Responses with
``Private`` or ``No-Store`` are now rejected by the cache, just like ``private`` or ``no-store``.
