Fixed a case where upstream HTTP filters could see decode callbacks after
``onDestroy()``. A synchronous connection-pool failure inside ``newStream()``
(for example circuit-breaker overflow) resets and cleans up the upstream request
before ``newStream()`` returns; the router no longer continues decode on that
destroyed filter chain. The request still returns ``503`` with the usual overflow
response flags.
