Fixed a race where a request arriving while DNS resolution for a cold host was completing could receive a spurious
503 (``UH`` with the successful resolver detail string) even though resolution succeeded. The cluster now re-selects
the host after observing a completed cache entry instead of treating a cache hit after a cluster-map miss as a failure.
