Fixed a bug where HTTP cache and cache_v2 responses with a ``max-age`` (or ``s-maxage``) but no
``Date`` header were considered stale and revalidated or refetched on every request. Per
:rfc:`9110#section-6.6.1`, a caching recipient that receives a response without a ``Date`` header
records the time it was received. The cache now falls back to the response time when no valid
``Date`` header is present, and no longer emits an empty ``If-Modified-Since`` header during
validation when neither ``Last-Modified`` nor ``Date`` is present.
