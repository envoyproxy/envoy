Fixed a bug in the HTTP server properties cache where an advertised ``SETTINGS_MAX_CONCURRENT_STREAMS``
update was silently dropped (and a stale value persisted to the key-value store) whenever the origin
already had a cache entry, for example one created by an earlier RTT or alt-svc write. The advertised
concurrency is now stored on updates, so new connections to known origins compute the correct initial
stream limit.
