Fixed a lifecycle violation where upstream HTTP filters could receive decode callbacks after
``onDestroy()`` had already run on them. When a connection pool fails synchronously inside
``newStream()`` -- for example on circuit breaker overflow for pending requests, connections, or
requests -- the upstream request is reset and scheduled for deferred deletion before ``newStream()``
returns, which destroys the upstream HTTP filter chain. The router then continued to call
``decodeHeaders()`` on the destroyed chain. Native C++ filters generally tolerate this, but filters
that release their state in ``onDestroy()`` do not: the dynamic modules HTTP filter nulls its
in-module filter pointer and crashes the worker when the destroyed filter is driven afterwards. The
router now stops driving the upstream filter chain when the upstream request was reset during
``newStream()``. The request still sheds with a ``503`` and the usual overflow response flags and
details.
