Changed local cluster rate limit sharing so a request that fits without sharing can consume a full
bucket when its scaled token cost would exceed ``max_tokens``. This prevents local cluster growth
from making the request impossible to admit. This behavior can be temporarily reverted by setting
runtime guard ``envoy.reloadable_features.local_ratelimit_local_cluster_preserve_one_request`` to
``false``. Because each member can consume a full bucket, the aggregate burst and admitted rate can
exceed the configured values. Requests that cost more than ``max_tokens`` without sharing remain
rejected, and ``max_tokens: 0`` continues to reject every request.
