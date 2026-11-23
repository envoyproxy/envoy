Fixed the cache and cache_v2 filters to treat cached responses as stale when their age equals
or exceeds their freshness lifetime, as required by RFC 9111. Responses with
``Cache-Control: max-age=0`` are now immediately stale instead of being considered fresh for up to one second.
