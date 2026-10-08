Fixed unbounded memory growth caused by ``TlsCachingDateProviderImpl::onRefreshDate()``
calling ``SlotImpl::set()`` every 500ms on the main thread while workers had not started,
leaving Date update callbacks queued in their dispatchers. This occurred when xDS was
unavailable during startup with ``initial_fetch_timeout: 0s``. Each thread now initializes
its Date cache immediately and refreshes it with a local timer. See
`issue #31561 <https://github.com/envoyproxy/envoy/issues/31561>`_.
