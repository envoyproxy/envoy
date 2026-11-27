The :ref:`rate limit quota <config_http_filters_rate_limit_quota>` filter now logs
``A new TokenBucket has been configured`` and ``The TokenBucket ... is carried over`` at ``debug`` level instead of ``info``.
Each line is emitted once per bucket in every RLQS response, which with per-user buckets means one line per active user.
Alerts or log pipelines that match these lines need the ``rate_limit_quota`` logger set to ``debug``.
