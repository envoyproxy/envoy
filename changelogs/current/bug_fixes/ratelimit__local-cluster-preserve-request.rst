Fixed local cluster rate limit sharing when the shared cost of a request is larger than
``max_tokens``, for example when the local cluster has more members than ``max_tokens``. Before this
fix, every such request was rejected. Now, a request that fits in the bucket without sharing passes
when the bucket holds ``max_tokens`` tokens. The request still consumes its full shared cost, so the
bucket can go below zero and the next request waits until the bucket refills. The aggregate rate
across the local cluster stays at the configured rate, but the aggregate burst can be one request
for each member. Requests that cost more than ``max_tokens`` without sharing remain rejected, and
``max_tokens: 0`` continues to reject every request. This behavior can be temporarily reverted by
setting runtime guard
``envoy.reloadable_features.local_ratelimit_local_cluster_preserve_one_request`` to ``false``.
