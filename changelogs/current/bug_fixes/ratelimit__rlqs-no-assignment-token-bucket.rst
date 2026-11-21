Fixed a crash in the ``envoy.filters.http.rate_limit_quota`` filter when a bucket's
``no_assignment_behavior`` used a ``token_bucket`` fallback: the second request into the bucket, or the
first one after an assignment expired, dereferenced a missing token bucket while no assignment from the
RLQS server was in effect.
