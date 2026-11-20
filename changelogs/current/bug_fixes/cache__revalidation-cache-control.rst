Fixed the cache_v2 filter to recheck cacheability after merging a 304 validation response. Responses
that become uncacheable, including those with ``Cache-Control: private`` or ``no-store``, are evicted
and fetched again without cache validators, preventing their headers from being shared with other
requests.
