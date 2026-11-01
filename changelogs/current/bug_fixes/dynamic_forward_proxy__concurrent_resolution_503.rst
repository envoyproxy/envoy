Fixed a race in the dynamic forward proxy cluster where a request arriving while DNS resolution for a cold host was
completing could receive a spurious 503 (``UH``) with the successful resolver detail string even though resolution
succeeded. The cluster now re-selects the host from its host map after observing a completed cache entry instead of
treating a cache hit following a host-map miss as a failure. As part of this change, host selection that completes
with no DNS host info now reports ``no_host`` as the response code details instead of an empty string.
