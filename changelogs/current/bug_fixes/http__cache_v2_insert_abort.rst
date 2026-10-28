http: fixed a crash in the cache v2 filter when a late callback from a failed cache insertion affected a subsequent
  request for the same entry. Aborted insertions now reset waiting requests, and subsequent requests use a new cache session.
  Lookups aborted without a response source now increment the cache ``upstream_reset`` event counter.
